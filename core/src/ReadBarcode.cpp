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
#include "ThresholdBinarizer.h"

#include "datamatrix/DMLadderSpec.h"
#include "datamatrix/DMLadderStats.h"
#include "datamatrix/DMReader.h"
#include "Point.h"

#include <algorithm>
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
static void ScaleToInput(Result& r, const ImageView& layer, const ImageView& _iv)
{
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
// An explicit walk from the phase's ladder string ("walk:L0/L0+inv/L1",
// DMLadderSpec.h): exactly these passes, in this order. A pass reuses the
// previous pass's bitmap when it is on the same layer and only inversion
// changes, as the built-in walk does; a pass with `close` closes before it
// inverts. A layer the pyramid does not have, or a closing pass with no format
// that benefits from closing, is skipped.
template <typename Body>
static Results WalkExplicit(const ImageView& _iv, const ImageView& iv, const DecodeHints& hints, Binarizer binarizer,
							const std::vector<DataMatrix::ladder::WalkPass>& passes, Body&& body)
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
	std::unique_ptr<BinaryBitmap> bitmap;
	size_t bitmapLayer = 0;
	bool closed = false, inverted = false;
	for (const auto& p : passes) {
		if (p.layer >= pyramid.layers.size() || (p.close && !closedReader))
			continue;
		auto&& layer = pyramid.layers[p.layer];
		if (!bitmap || bitmapLayer != p.layer || (closed && !p.close)) {
			bitmap = CreateBitmap(binarizer, layer);
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
			CRPT_LADDER_PASS(binarizer, p.layer, p.close, p.invert);
			[[maybe_unused]] size_t resultsBefore = results.size();
			done = body(*bitmap, layer, p.close, closedReader.get(), results, maxSymbols);
			CRPT_LADDER_PASS_RESULTS(results, resultsBefore);
		}
		if (done || maxSymbols <= 0)
			return results;
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
	if (const std::string& ladder = hints.dmLadder();
		ladder.find("walk:") != std::string::npos || ladder.find("bin:") != std::string::npos) {
		const DataMatrix::ladder::Spec spec = DataMatrix::ladder::ParseSpec(ladder);
		if (spec.binarizer >= 0)
			binarizer = static_cast<Binarizer>(spec.binarizer);
		if (!spec.walk.empty())
			return WalkExplicit(_iv, iv, hints, binarizer, spec.walk, body);
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
	for (size_t layerIndex = 0; layerIndex < pyramid.layers.size(); ++layerIndex) {
		auto&& layer = pyramid.layers[layerIndex];
		auto bitmap = CreateBitmap(binarizer, layer);
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
					CRPT_LADDER_PASS(binarizer, layerIndex, close != 0, invert != 0);
					[[maybe_unused]] size_t resultsBefore = results.size();
					done = body(*bitmap, layer, close != 0, closedReader.get(), results, maxSymbols);
					CRPT_LADDER_PASS_RESULTS(results, resultsBefore);
				}
				if (done)
					return results;
				if (maxSymbols <= 0)
					return results;
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

	return WalkBinarizations(_iv, iv, hints, [&](BinaryBitmap& bitmap, const ImageView& layer, bool close, MultiFormatReader* closedReader, Results& results, int& maxSymbols) {
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
	return WalkBinarizations(_iv, iv, hints, [&](BinaryBitmap& bitmap, const ImageView& layer, bool, MultiFormatReader*, Results& results, int&) {
		PointF layerScale(layer.width(), layer.height());

		PointF sP0 = P0 * layerScale;
		PointF sP1 = P1 * layerScale;
		PointF sP2 = P2 * layerScale;
		PointF sP3 = P3 * layerScale;

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

	return WalkBinarizations(_iv, iv, hints, [&](BinaryBitmap& bitmap, const ImageView& layer, bool close, MultiFormatReader* closedReader, Results& results, int& maxSymbols) {
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
