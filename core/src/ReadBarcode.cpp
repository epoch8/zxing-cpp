/*
* Copyright 2019 Axel Waggershauser
*/
// SPDX-License-Identifier: Apache-2.0

#include "ReadBarcode.h"

#include "DecodeHints.h"
#include "GlobalHistogramBinarizer.h"
#include "HybridBinarizer.h"
#include "MultiFormatReader.h"
#include "Pattern.h"
#include "Preprocess.h"
#include "PreprocessGeometry.h"
#include "ThresholdBinarizer.h"

#include "datamatrix/DMLadderSpec.h"
#include "datamatrix/DMLadderStats.h"
#include "datamatrix/DMReader.h"
#include "Point.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <memory>
#include <stdexcept>

namespace ZXing {

class LumImage : public ImageView
{
	std::unique_ptr<uint8_t[]> _memory;
	LumImage(std::unique_ptr<uint8_t[]>&& data, int w, int h)
		: ImageView(data.get(), w, h, ImageFormat::Lum), _memory(std::move(data))
	{}

public:
	LumImage() : ImageView(nullptr, 0, 0, ImageFormat::Lum) {}
	LumImage(int w, int h) : LumImage(std::make_unique<uint8_t[]>(w * h), w, h) {}

	uint8_t* data() { return _memory.get(); }
};

template<typename P>
static LumImage ExtractLum(const ImageView& iv, P projection)
{
	LumImage res(iv.width(), iv.height());

	auto* dst = res.data();
	for(int y = 0; y < iv.height(); ++y)
		for(int x = 0, w = iv.width(); x < w; ++x)
			*dst++ = projection(iv.data(x, y));

	return res;
}

class LumImagePyramid
{
	std::vector<LumImage> buffers;

	template<int N>
	void addLayer()
	{
		auto siv = layers.back();
		buffers.emplace_back(siv.width() / N, siv.height() / N);
		layers.push_back(buffers.back());
		auto& div = buffers.back();
		auto* d   = div.data();

		for (int dy = 0; dy < div.height(); ++dy)
			for (int dx = 0; dx < div.width(); ++dx) {
				int sum = (N * N) / 2;
				for (int ty = 0; ty < N; ++ty)
					for (int tx = 0; tx < N; ++tx)
						sum += *siv.data(dx * N + tx, dy * N + ty);
				*d++ = sum / (N * N);
			}
	}

	void addLayer(int factor)
	{
		// help the compiler's auto-vectorizer by hard-coding the scale factor
		switch (factor) {
		case 2: addLayer<2>(); break;
		case 3: addLayer<3>(); break;
		case 4: addLayer<4>(); break;
		default: throw std::invalid_argument("Invalid DecodeHints::downscaleFactor"); break;
		}
	}

public:
	std::vector<ImageView> layers;

	LumImagePyramid(const ImageView& iv, int threshold, int factor)
	{
		layers.push_back(iv);
		// TODO: if only matrix codes were considered, then using std::min would be sufficient (see #425)
		while (threshold > 0 && std::max(layers.back().width(), layers.back().height()) > threshold &&
			   std::min(layers.back().width(), layers.back().height()) >= factor)
			addLayer(factor);
#if 0
		// Reversing the layers means we'd start with the smallest. that can make sense if we are only looking for a
		// single symbol. If we start with the higher resolution, we get better (high res) position information.
		// TODO: see if masking out higher res layers based on found symbols in lower res helps overall performance.
		std::reverse(layers.begin(), layers.end());
#endif
	}
};

ImageView SetupLumImageView(ImageView iv, LumImage& lum, const DecodeHints& hints)
{
	if (iv.format() == ImageFormat::None)
		throw std::invalid_argument("Invalid image format");

	if (hints.binarizer() == Binarizer::GlobalHistogram || hints.binarizer() == Binarizer::LocalAverage) {
		if (iv.format() != ImageFormat::Lum) {
			lum = ExtractLum(iv, [r = RedIndex(iv.format()), g = GreenIndex(iv.format()), b = BlueIndex(iv.format())](
									 const uint8_t* src) { return RGBToLum(src[r], src[g], src[b]); });
		} else if (iv.pixStride() != 1) {
			// GlobalHistogram and LocalAverage need dense line memory layout
			lum = ExtractLum(iv, [](const uint8_t* src) { return *src; });
		}
		if (lum.data())
			return lum;
	}
	return iv;
}

std::unique_ptr<BinaryBitmap> CreateBitmap(ZXing::Binarizer binarizer, const ImageView& iv)
{
	switch (binarizer) {
	case Binarizer::BoolCast: return std::make_unique<ThresholdBinarizer>(iv, 0);
	case Binarizer::FixedThreshold: return std::make_unique<ThresholdBinarizer>(iv, 127);
	case Binarizer::GlobalHistogram: return std::make_unique<GlobalHistogramBinarizer>(iv);
	case Binarizer::LocalAverage: return std::make_unique<HybridBinarizer>(iv);
	}
	return {}; // silence gcc warning
}

Result ReadBarcode(const ImageView& _iv, const DecodeHints& hints)
{
	return FirstOrDefault(ReadBarcodes(_iv, DecodeHints(hints).setMaxNumberOfSymbols(1)));
}

static void ThrowIfTooLarge(const ImageView& _iv, const DecodeHints& hints)
{
	if (sizeof(PatternType) < 4 && hints.hasFormat(BarcodeFormat::LinearCodes) && (_iv.width() > 0xffff || _iv.height() > 0xffff))
		throw std::invalid_argument("maximum image width/height is 65535");
}

// A result found on a pyramid layer, mapped back to the caller's image.
// One layer as a walk body sees it: `layer` is the pyramid layer, `view` what
// was binarized -- the layer itself, or the layer after the "pre:" ops, with
// `toLayer` mapping view coordinates back to the layer's (ADR 0006 step 8).
struct WalkLayer
{
	ImageView layer;
	ImageView view;
	bool mapped = false;
	Affine toLayer, fromLayer;

	PointF fromLayerPoint(PointF p) const
	{
		if (!mapped)
			return p;
		const double* m = fromLayer.m;
		return {m[0] * p.x + m[1] * p.y + m[2], m[3] * p.x + m[4] * p.y + m[5]};
	}
};

static Position MapPosition(const Position& p, const Affine& a)
{
	Position out = p;
	const double* m = a.m;
	for (int i = 0; i < 4; ++i) {
		const double x = p[i].x, y = p[i].y;
		out[i] = PointI(int(std::lround(m[0] * x + m[1] * y + m[2])), int(std::lround(m[3] * x + m[4] * y + m[5])));
	}
	return out;
}

// From the binarized view to the input: first back through the "pre:" ops'
// map, if any, then the pyramid layer's scale as before.
static void ScaleToInput(Result& r, const WalkLayer& wl, const ImageView& _iv)
{
	if (wl.mapped)
		r.setPosition(MapPosition(r.position(), wl.toLayer));
	const ImageView& layer = wl.layer;
	if (layer.width() != _iv.width())
		r.setPosition(Scale(r.position(), _iv.width() / layer.width()));
}

// The pyramid x close x invert walk that every entry point below shares --
// there used to be three copies of it. `body` is handed each binarized layer
// in turn and returns true to stop; it owns what goes into `results` and when
// `maxSymbols` is decremented, because the three callers collect differently
// on purpose (dedup vs. first-hit, and what runs on the closed pass).
//
// The nesting is kept exactly, including that the `close` pass operates on
// whatever state `invert` left the bitmap in -- see the TODO below.
// The pyramid layers with the "pre:" ops applied (ADR 0006 step 8): each layer
// is processed once, on first use, and kept for that layer's passes, with the
// composed affine map of the ops (layer -> processed; PreprocessGeometry.h) so
// results can be placed back on the layer. An op that fails, or that does not
// give back one channel, is skipped: the image and the map stay as they were.
// Without "pre:" a layer is returned as it is.
// A "pre:" op's arguments as PreprocessParams, in the order the token gives
// them: SauvolaBinarization(window:k:R), ClaheEnhancement(clipLimit:gridSize),
// Top/BlackHatTransformation(kernelSize), AnisotropicDiffusion(iterations:
// lambda:k), SuperResolution(scale), Erode(whiteOnBlack 0/1:size),
// Rotate(degrees), CenterCrop(factor). Omitted ones keep their defaults.
static PreprocessParams PreParams(const DataMatrix::ladder::PreOp& p)
{
	PreprocessParams q;
	const auto a = [&](int i, double d) { return i < p.nargs ? p.args[i] : d; };
	switch (static_cast<PreprocessOp>(p.op)) {
	case PreprocessOp::SauvolaBinarization:
		q.window = int(a(0, q.window)), q.sauvolaK = a(1, q.sauvolaK), q.sauvolaR = a(2, q.sauvolaR);
		break;
	case PreprocessOp::ClaheEnhancement: q.clipLimit = a(0, q.clipLimit), q.gridSize = int(a(1, q.gridSize)); break;
	case PreprocessOp::TopHatTransformation:
	case PreprocessOp::BlackHatTransformation: q.kernelSize = int(a(0, q.kernelSize)); break;
	case PreprocessOp::AnisotropicDiffusion:
		q.iterations = int(a(0, q.iterations)), q.lambda = a(1, q.lambda), q.diffusionK = a(2, q.diffusionK);
		break;
	case PreprocessOp::SuperResolution: q.scale = a(0, q.scale); break;
	case PreprocessOp::Erode: q.whiteOnBlack = a(0, q.whiteOnBlack ? 1 : 0) != 0, q.erosionSize = int(a(1, q.erosionSize)); break;
	case PreprocessOp::Rotate: q.angleDegrees = a(0, q.angleDegrees); break;
	case PreprocessOp::CenterCrop: q.factor = a(0, q.factor); break;
	default: break;
	}
	return q;
}

class PreppedLayers
{
	const std::vector<DataMatrix::ladder::PreOp>& _ops;
	std::vector<PreprocessImage> _images;
	std::vector<Affine> _maps;
	std::vector<char> _done;

public:
	PreppedLayers(const std::vector<DataMatrix::ladder::PreOp>& ops, size_t layers)
		: _ops(ops), _images(ops.empty() ? 0 : layers), _maps(_images.size()), _done(_images.size(), 0)
	{}

	WalkLayer operator()(const ImageView& layer, size_t index)
	{
		WalkLayer wl{layer, layer};
		if (_ops.empty())
			return wl;
		PreprocessImage& img = _images[index];
		Affine& map = _maps[index];
		if (!_done[index]) {
			_done[index] = 1;
			ImageView in = layer;
			PreprocessImage a, b;
			PreprocessImage* out = &a;
			bool any = false;
			for (const auto& pre : _ops) {
				const auto op = static_cast<PreprocessOp>(pre.op);
				const PreprocessParams params = PreParams(pre);
				if (!Preprocess(op, params, in, *out) || out->channels != 1 || out->empty())
					continue;
				map = PreprocessGeometry(op, params, in.width(), in.height(), out->width, out->height).after(map);
				in = ImageView(out->pixels.data(), out->width, out->height, ImageFormat::Lum);
				out = (out == &a) ? &b : &a;
				any = true;
			}
			if (any)
				img = std::move(out == &a ? b : a); // the last one written
		}
		if (img.empty())
			return wl;
		wl.view = ImageView(img.pixels.data(), img.width, img.height, ImageFormat::Lum);
		wl.mapped = !map.identity();
		wl.fromLayer = map;
		wl.toLayer = map.inverse();
		return wl;
	}
};

// Record runs (ADR 0006 step 4: the calculator adds +sweepSteps to the
// DataMatrix ladder string). A walk that would stop -- a body said done, or the
// symbol budget ran out -- runs its remaining passes anyway, each into scratch
// results that are thrown away, so the ladder rows record what every pass can
// read on its own. What the walk returns does not change.
static bool SweepWalk([[maybe_unused]] const DecodeHints& hints)
{
#ifdef CRPT_ZXING_PROFILING
	return hints.dmLadder().find("+sweepSteps") != std::string::npos;
#else
	return false;
#endif
}

// An explicit walk from the phase's ladder string ("walk:L0/L0+inv/L1",
// DMLadderSpec.h): exactly these passes, in this order. A pass reuses the
// previous pass's bitmap when it is on the same layer and only inversion
// changes, as the built-in walk does; a pass with `close` closes before it
// inverts. A layer the pyramid does not have, or a closing pass with no format
// that benefits from closing, is skipped.
template <typename Body>
static Results WalkExplicit(const ImageView& _iv, const ImageView& iv, const DecodeHints& hints, Binarizer binarizer,
							const std::vector<DataMatrix::ladder::WalkPass>& passes, const std::vector<DataMatrix::ladder::PreOp>& preOps,
							Body&& body)
{
	auto formatsBenefittingFromClosing = BarcodeFormat::Aztec | BarcodeFormat::DataMatrix | BarcodeFormat::QRCode | BarcodeFormat::MicroQRCode;
	std::unique_ptr<MultiFormatReader> closedReader;
	const bool anyClose = std::any_of(passes.begin(), passes.end(), [](auto& p) { return p.close; });
	if (anyClose && hints.hasFormat(formatsBenefittingFromClosing)) {
		DecodeHints closedHints = hints;
		closedHints.setFormats((hints.formats().empty() ? BarcodeFormat::Any : hints.formats()) & formatsBenefittingFromClosing);
		closedReader = std::make_unique<MultiFormatReader>(closedHints);
	}

	LumImagePyramid pyramid = [&] {
		CRPT_LADDER_WALK_SETUP(Pyramid);
		return LumImagePyramid(iv, hints.downscaleThreshold() * hints.tryDownscale(), hints.downscaleFactor());
	}();

	Results results;
	int maxSymbols = hints.maxNumberOfSymbols() ? hints.maxNumberOfSymbols() : INT_MAX;
	const bool sweep = SweepWalk(hints);
	Results scratch;
	int scratchMax = 0;
	bool stopped = false; // a sweep past the point where the walk returns
	PreppedLayers prepped(preOps, pyramid.layers.size());
	std::unique_ptr<BinaryBitmap> bitmap;
	size_t bitmapLayer = 0;
	bool closed = false, inverted = false;
	for (const auto& p : passes) {
		if (p.layer >= pyramid.layers.size() || (p.close && !closedReader))
			continue;
		const WalkLayer layer = prepped(pyramid.layers[p.layer], p.layer);
		if (!bitmap || bitmapLayer != p.layer || (closed && !p.close)) {
			bitmap = CreateBitmap(binarizer, layer.view);
			bitmapLayer = p.layer;
			closed = inverted = false;
		}
		if (p.close && !closed) {
			if (inverted) {
				bitmap->invert();
				inverted = false;
			}
			CRPT_LADDER_WALK_SETUP(Close);
			bitmap->close();
			closed = true;
		}
		if (inverted != p.invert) {
			CRPT_LADDER_WALK_SETUP(Invert);
			bitmap->invert();
			inverted = p.invert;
		}
		bool done;
		{
			if (stopped) {
				scratch.clear();
				scratchMax = hints.maxNumberOfSymbols() ? hints.maxNumberOfSymbols() : INT_MAX;
			}
			Results& into = stopped ? scratch : results;
			CRPT_LADDER_PASS(binarizer, p.layer, p.close, p.invert);
			[[maybe_unused]] size_t resultsBefore = into.size();
			done = body(*bitmap, layer, p.close, closedReader.get(), into, stopped ? scratchMax : maxSymbols);
			CRPT_LADDER_PASS_RESULTS(into, resultsBefore);
		}
		if (!stopped && (done || maxSymbols <= 0)) {
			if (!sweep)
				return results;
			stopped = true;
		}
	}
	return results;
}

template <typename Body>
static Results WalkBinarizations(const ImageView& _iv, const ImageView& iv, const DecodeHints& hints, Body&& body)
{
	// The phase's ladder string can set the walk ("walk:", "bin:" --
	// DMLadderSpec.h). Parsed only when it mentions one, so the usual call
	// parses nothing; the string was validated when the graph opened.
	Binarizer binarizer = hints.binarizer();
	std::vector<DataMatrix::ladder::PreOp> preOps;
	if (const std::string& ladder = hints.dmLadder(); ladder.find("walk:") != std::string::npos ||
													  ladder.find("bin:") != std::string::npos || ladder.find("pre:") != std::string::npos) {
		const DataMatrix::ladder::Spec spec = DataMatrix::ladder::ParseSpec(ladder);
		if (spec.binarizer >= 0)
			binarizer = static_cast<Binarizer>(spec.binarizer);
		preOps = spec.preOps;
		if (!spec.walk.empty())
			return WalkExplicit(_iv, iv, hints, binarizer, spec.walk, preOps, body);
	}

	auto formatsBenefittingFromClosing = BarcodeFormat::Aztec | BarcodeFormat::DataMatrix | BarcodeFormat::QRCode | BarcodeFormat::MicroQRCode;
	DecodeHints closedHints = hints;
	std::unique_ptr<MultiFormatReader> closedReader;
	if (hints.tryDenoise() && hints.hasFormat(formatsBenefittingFromClosing)) {
		closedHints.setFormats((hints.formats().empty() ? BarcodeFormat::Any : hints.formats()) & formatsBenefittingFromClosing);
		closedReader = std::make_unique<MultiFormatReader>(closedHints);
	}

	LumImagePyramid pyramid = [&] {
		CRPT_LADDER_WALK_SETUP(Pyramid);
		return LumImagePyramid(iv, hints.downscaleThreshold() * hints.tryDownscale(), hints.downscaleFactor());
	}();

	Results results;
	int maxSymbols = hints.maxNumberOfSymbols() ? hints.maxNumberOfSymbols() : INT_MAX;
	const bool sweep = SweepWalk(hints);
	Results scratch;
	int scratchMax = 0;
	bool stopped = false; // a sweep past the point where the walk returns
	PreppedLayers prepped(preOps, pyramid.layers.size());
	for (size_t layerIndex = 0; layerIndex < pyramid.layers.size(); ++layerIndex) {
		const WalkLayer layer = prepped(pyramid.layers[layerIndex], layerIndex);
		auto bitmap = CreateBitmap(binarizer, layer.view);
		for (int close = 0; close <= (closedReader ? 1 : 0); ++close) {
			if (close) {
				CRPT_LADDER_WALK_SETUP(Close);
				bitmap->close();
			}

			// TODO: check if closing after invert would be beneficial
			for (int invert = 0; invert <= static_cast<int>(hints.tryInvert() && !close); ++invert) {
				if (invert) {
					CRPT_LADDER_WALK_SETUP(Invert);
					bitmap->invert();
				}

				// Ladder attribution: name the pass each returned result came from
				// (DMLadderStats.h). Compiled out without CRPT_ZXING_PROFILING.
				bool done;
				{
					if (stopped) {
						scratch.clear();
						scratchMax = hints.maxNumberOfSymbols() ? hints.maxNumberOfSymbols() : INT_MAX;
					}
					Results& into = stopped ? scratch : results;
					CRPT_LADDER_PASS(binarizer, layerIndex, close != 0, invert != 0);
					[[maybe_unused]] size_t resultsBefore = into.size();
					done = body(*bitmap, layer, close != 0, closedReader.get(), into, stopped ? scratchMax : maxSymbols);
					CRPT_LADDER_PASS_RESULTS(into, resultsBefore);
				}
				if (!stopped && (done || maxSymbols <= 0)) {
					if (!sweep)
						return results;
					stopped = true;
				}
			}
		}
	}

	return results;
}

Results ReadBarcodes(const ImageView& _iv, const DecodeHints& hints)
{
	ThrowIfTooLarge(_iv, hints);

	LumImage lum;
	ImageView iv = [&] {
		CRPT_LADDER_WALK_SETUP(Lum);
		return SetupLumImageView(_iv, lum, hints);
	}();
	MultiFormatReader reader(hints);

	if (hints.isPure())
		return {reader.read(*CreateBitmap(hints.binarizer(), iv))};

	return WalkBinarizations(_iv, iv, hints, [&](BinaryBitmap& bitmap, const WalkLayer& layer, bool close, MultiFormatReader* closedReader, Results& results, int& maxSymbols) {
		auto rs = (close ? *closedReader : reader).readMultiple(bitmap, maxSymbols);
		for (auto& r : rs) {
			ScaleToInput(r, layer, _iv);
			if (!Contains(results, r)) {
				r.setDecodeHints(hints);
				r.setIsInverted(bitmap.inverted());
				results.push_back(std::move(r));
				--maxSymbols;
			}
		}
		return false;
	});
}

Results readbarcodescrpt_detector_v1_samplegridv1(const ImageView& _iv, const PointF& P0, const PointF& P1, const PointF& P2, const PointF& P3, const DecodeHints& hints)
{
	ThrowIfTooLarge(_iv, hints);

	LumImage lum;
	ImageView iv = [&] {
		CRPT_LADDER_WALK_SETUP(Lum);
		return SetupLumImageView(_iv, lum, hints);
	}();

	auto reader = std::make_unique<ZXing::DataMatrix::Reader>(hints);

	// This entry point has always run `reader` on the closed pass too, never
	// the MultiFormatReader the walk builds for it, and returns on the first
	// valid result without the Contains() dedup. Both kept as they were.
	return WalkBinarizations(_iv, iv, hints, [&](BinaryBitmap& bitmap, const WalkLayer& layer, bool, MultiFormatReader*, Results& results, int&) {
		PointF layerScale(layer.layer.width(), layer.layer.height());

		// The corner hints on the layer, then into the binarized view ("pre:").
		PointF sP0 = layer.fromLayerPoint(P0 * layerScale);
		PointF sP1 = layer.fromLayerPoint(P1 * layerScale);
		PointF sP2 = layer.fromLayerPoint(P2 * layerScale);
		PointF sP3 = layer.fromLayerPoint(P3 * layerScale);

		auto r = reader->decode(bitmap, sP0, sP1, sP2, sP3);
		if (r.isValid()) {
			ScaleToInput(r, layer, _iv);
			r.setDecodeHints(hints);
			r.setIsInverted(bitmap.inverted());
			results.push_back(std::move(r));
			return true;
		}
		return false;
	});
}

Results readbarcodescrpt_samplegridv1(const ImageView& _iv, const DecodeHints& hints, bool returnEdges)
{
	ThrowIfTooLarge(_iv, hints);

	LumImage lum;
	ImageView iv = [&] {
		CRPT_LADDER_WALK_SETUP(Lum);
		return SetupLumImageView(_iv, lum, hints);
	}();

	auto reader = std::make_unique<ZXing::DataMatrix::DMCRPTReader>(hints);

	return WalkBinarizations(_iv, iv, hints, [&](BinaryBitmap& bitmap, const WalkLayer& layer, bool close, MultiFormatReader* closedReader, Results& results, int& maxSymbols) {
		if(!close) {
			auto r = reader->decode(bitmap);
			if (r.isValid()) {
				ScaleToInput(r, layer, _iv);
				if (!Contains(results, r)) {
					r.setDecodeHints(hints);
					r.setIsInverted(bitmap.inverted());
					results.push_back(std::move(r));
					--maxSymbols;
				}
			} else if(returnEdges) {
				results.push_back(std::move(r));
			}
		} else {
			auto rs = closedReader->readMultiple(bitmap, maxSymbols);
			for (auto& r : rs) {
				ScaleToInput(r, layer, _iv);
				if (!Contains(results, r)) {
					r.setDecodeHints(hints);
					r.setIsInverted(bitmap.inverted());
					results.push_back(std::move(r));
					--maxSymbols;
				}
			}
		}
		return false;
	});
}

} // ZXing
