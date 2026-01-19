#include "ReadBarcode.h"

#include <iostream>
#include "ImageView.h"
#include "Result.h"
#include "opencv2/core.hpp"

#include "BinaryBitmap.h"
#include "DecodeHints.h"
#include "GlobalHistogramBinarizer.h"
#include "HybridBinarizer.h"
#include "MultiFormatReader.h"
#include "Pattern.h"
#include "ThresholdBinarizer.h"
#include "UnwarpPreprocess.h"

#include "datamatrix/DMReader.h"
#include "Point.h"

#include <emscripten/bind.h>

using namespace emscripten;

namespace ZXing {

	inline ImageView ImageViewFromMat(const cv::Mat& image)
	{
		auto fmt = ImageFormat::None; // Use the fully qualified name
		switch (image.channels()) {
		case 1: fmt = ImageFormat::Lum; break; // Use the fully qualified name
		case 3: fmt = ImageFormat::BGR; break; // Use the fully qualified name
		case 4: fmt = ImageFormat::BGRX; break; // Use the fully qualified name
		}

		if (image.depth() != CV_8U || fmt == ImageFormat::None)
			return { nullptr, 0, 0, ImageFormat::None }; // Use the fully qualified name

		return { image.data, image.cols, image.rows, fmt };
	}
}

class PreprocessingPipeline {
public:
	enum class Mode { SEQUENTIAL, PARALLEL };
	using ImageOperation = std::function<cv::Mat(const cv::Mat&)>;
	using DetectionFunction = std::function<std::unique_ptr<ZXing::Results>(const cv::Mat&, const ZXing::DecodeHints&, const std::string&)>;

	explicit PreprocessingPipeline(Mode mode = Mode::SEQUENTIAL) : mode_(mode) {}

	PreprocessingPipeline& addOperation(ImageOperation op, const std::string& name) {
		operations_.emplace_back(std::move(op), name);
		return *this;
	}

	// Unified execution: Both modes use same pattern with different input sources
	std::unique_ptr<ZXing::Results> execute(const cv::Mat& input,
				   const ZXing::DecodeHints& hints,
				   const DetectionFunction& detector) const {

		const std::string mode_name = (mode_ == Mode::PARALLEL) ? "Parallel" : "Sequential";

		cv::Mat working_image;
		if (mode_ == Mode::SEQUENTIAL) {
			working_image = input.clone();  // Track sequential progress
		}

		for (const auto& [operation, name] : operations_) {
			// Choose input source based on mode - this is the key insight!
			auto& operation_input = (mode_ == Mode::PARALLEL)
				? input           // Parallel: always use original image
				: working_image;  // Sequential: use result from previous operation

			cv::Mat processed = operation(operation_input);

			if (!processed.empty()) {

				// Try barcode detection immediately (same for both modes)
				auto zxing_results = detector(processed, hints, name);

				if (zxing_results && zxing_results->size() > 0) {
					return zxing_results;  // This preprocessing technique worked!
				}

				// Update working image for next sequential operation
				if (mode_ == Mode::SEQUENTIAL) {
					working_image = processed;
				}

			}
			else {

				// For sequential mode, empty result breaks the chain
				if (mode_ == Mode::SEQUENTIAL) {
					return nullptr;
				}
			}
		}

		return nullptr;  // All techniques failed
	}

	// Legacy method for backward compatibility (without detection)

	std::vector<std::string> getOperationNames() const {
		std::vector<std::string> names;
		for (const auto& [op, name] : operations_) {
			names.push_back(name);
		}
		return names;
	}

	Mode getMode() const { return mode_; }

private:
	Mode mode_;
	std::vector<std::pair<ImageOperation, std::string>> operations_;
};

class PreprocessingConstructor {
public:
	explicit PreprocessingConstructor(PreprocessingPipeline::Mode mode = PreprocessingPipeline::Mode::SEQUENTIAL)
		: pipeline_(mode) {}

	// Helper function to match original implementation
	static cv::Mat darkenImage_smart_ladder(const cv::Mat& image, int darken_amount) {
		cv::Mat darkened;
		cv::convertScaleAbs(image, darkened, 1.0, -darken_amount);
		return darkened;
	}

	static cv::Mat grayscaleIfNecessary(const cv::Mat& img, cv::Mat& image_gr) {
		switch (img.channels())
		{
		case 1:
			image_gr = img.clone(); break;
			break;
		case 3:
			cv::cvtColor(img, image_gr, cv::COLOR_BGR2GRAY);
			break;
		case 4:
			cv::cvtColor(img, image_gr, cv::COLOR_BGRA2GRAY);
		default:
			throw std::invalid_argument("Unsupported image channels count");
		}
		return image_gr;
	}

	// Case 0: Original (no processing)
	PreprocessingConstructor& original() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			return img.clone();
		},
			"Original"
		);
		return *this;
	}

	// Case 1: Adaptive threshold with original parameters
	PreprocessingConstructor& adaptiveThreshold() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			cv::Mat image_gr, result;
			grayscaleIfNecessary(img, image_gr);
			cv::adaptiveThreshold(image_gr, result, 255, 0, 0, 37, 2);
			return result;
		},
			"AdaptiveThreshold"
		);
		return *this;
	}

	// Case 2: Unsharp mask with original parameters (skip if isWhiteOnBlack)
	PreprocessingConstructor& unsharpMask() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			cv::Mat blurred1, result;
			cv::GaussianBlur(img, blurred1, cv::Size(23, 23), 0);
			cv::addWeighted(img, 1.9, blurred1, -0.9, 0, result);
			return result;
		},
			"UnsharpMask"
		);
		return *this;
	}

	// Case 3: Adaptive threshold with MEAN_C
	PreprocessingConstructor& adaptiveMean() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			cv::Mat image_gr, result;
			grayscaleIfNecessary(img, image_gr);
			cv::adaptiveThreshold(image_gr, result, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, 17, 4);
			return result;
		},
			"AdaptiveMean"
		);
		return *this;
	}

	// Case 4: Complex blur + threshold operation
	PreprocessingConstructor& blurThreshold() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			cv::Mat blurred, image_mixed, image_gr2, result;
			cv::GaussianBlur(img, blurred, cv::Size(15, 15), 0);
			cv::addWeighted(img, 1.8485371046459401, blurred, -0.8485371046459401, 0, image_mixed);
			grayscaleIfNecessary(image_mixed, image_gr2);
			cv::threshold(image_gr2, result, 100, 255, 2);
			return result;
		},
			"BlurThreshold"
		);
		return *this;
	}

	// Case 5: Another complex blur + threshold with different parameters
	PreprocessingConstructor& blurThreshold2() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			cv::Mat blurred, image_mixed, image_gr2, result;
			cv::GaussianBlur(img, blurred, cv::Size(45, 45), 0);
			cv::addWeighted(img, 1.5037081148746935, blurred, -0.5037081148746935, 0, image_mixed);
			grayscaleIfNecessary(image_mixed, image_gr2);
			cv::threshold(image_gr2, result, 132, 255, 1);
			return result;
		},
			"BlurThreshold2"
		);
		return *this;
	}

	// Case 6: Complex scaling + resize + blur operation (skip if isWhiteOnBlack)
	PreprocessingConstructor& scaledUnsharp() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			cv::Mat image_scaled, image_resized, blurred, image_mixed, result;
			cv::convertScaleAbs(img, image_scaled, 0.7448063260333111, 30);
			cv::resize(image_scaled, image_resized, cv::Size(int(img.cols * 2.1), int(img.rows * 2.1)), cv::INTER_LINEAR);
			cv::GaussianBlur(image_resized, blurred, cv::Size(49, 49), 0);
			cv::addWeighted(image_resized, 1.9705662110228968, blurred, -0.9705662110228968, 0, image_mixed);
			grayscaleIfNecessary(image_mixed, result);
			return result;
		},
			"ScaledUnsharp"
		);
		return *this;
	}

	// Case 7: OTSU threshold with bitwise_not
	PreprocessingConstructor& otsuInverted() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			cv::Mat image_gr3, binary_image, result;
			grayscaleIfNecessary(img, image_gr3);
			cv::threshold(image_gr3, binary_image, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);
			cv::bitwise_not(binary_image, result);
			return result;
		},
			"OtsuInverted"
		);
		return *this;
	}

	// Case 8: Histogram equalization + sharpening (skip if isWhiteOnBlack)
	PreprocessingConstructor& histogramSharpening() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			cv::Mat darkened_image, darkened_image_gr, equalized, blurred2, result;
			cv::Mat sharpen_kernel;

			darkened_image = darkenImage_smart_ladder(img, 50);
			grayscaleIfNecessary(darkened_image, darkened_image_gr);

			cv::equalizeHist(darkened_image_gr, equalized);
			cv::GaussianBlur(equalized, blurred2, cv::Size(23, 23), 0);
			sharpen_kernel = (cv::Mat_<float>(3, 3) << -1, -1, -1, -1, 9, -1, -1, -1, -1);
			cv::filter2D(blurred2, result, -1, sharpen_kernel);

			return result;
		},
			"HistogramSharpening"
		);
		return *this;
	}

	// Case 9: Complex morphological operations
	PreprocessingConstructor& morphological() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			try {
				cv::Mat darkened_image, darkened_image_gr, blurred3, thresholded1, eroded, result;

				darkened_image = darkenImage_smart_ladder(img, 50);
				grayscaleIfNecessary(darkened_image, darkened_image_gr);

				cv::GaussianBlur(darkened_image_gr, blurred3, cv::Size(11, 11), 0);
				cv::threshold(blurred3, thresholded1, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);

				// Validate input before morphology to avoid OpenCV exceptions
				if (thresholded1.empty() || thresholded1.depth() != CV_8U) {
					return cv::Mat();
				}

				cv::Mat kernel = cv::Mat::ones(3, 3, CV_8U);
				cv::erode(thresholded1, eroded, kernel, cv::Point(-1, -1), 1);
				cv::dilate(eroded, result, kernel, cv::Point(-1, -1), 1);

				return result;
			}
			catch (const cv::Exception& e) {
				return cv::Mat();
			}
			catch (...) {
				return cv::Mat();
			}
		},
			"Morphological"
		);
		return *this;
	}

	// Case 10: Black enhancement by squaring pixel values (makes dark areas darker)
	PreprocessingConstructor& blackEnhancement() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			cv::Mat result, float_mat, squared_mat;

			// Convert to float32 for precise calculations
			img.convertTo(float_mat, CV_32F);

			// Square the image (multiply by itself)
			cv::multiply(float_mat, float_mat, squared_mat);

			// Normalize result to 0-255 range
			cv::normalize(squared_mat, squared_mat, 0.0, 255.0, cv::NORM_MINMAX);

			// Convert back to 8-bit format
			squared_mat.convertTo(result, CV_8U);
			return result;
		},
			"BlackEnhancement"
		);
		return *this;
	}

	// Case 10: Boost white regions using normalization, gamma correction, and blending
	PreprocessingConstructor& boostWhite() {
		pipeline_.addOperation(
			[](const cv::Mat& img) {
			cv::Mat result, white, gamma_corrected, normalized;
			img.copyTo(result);
			img.copyTo(white);

			// Normalize to 0-360 range
			cv::normalize(white, white, 0.0, 360.0, cv::NORM_MINMAX);

			// Apply gamma correction with gamma = 0.9
			double gamma = 0.9;
			double inv_gamma = 1.0 / gamma;
			cv::Mat lut(1, 256, CV_8U);
			uchar* p = lut.ptr();
			for (int i = 0; i < 256; ++i) {
				p[i] = cv::saturate_cast<uchar>(pow((double)i / 255.0, inv_gamma) * 255.0);
			}
			cv::LUT(white, lut, gamma_corrected);

			// Blend original (30%) with gamma-corrected (70%)
			cv::addWeighted(result, 0.3, gamma_corrected, 0.7, 0.0, result);

			// Normalize back to 0-255 range
			cv::normalize(result, result, 0.0, 255.0, cv::NORM_MINMAX);

			// Apply black enhancement (multiply by itself)
			cv::Mat float_mat, squared_mat;
			result.convertTo(float_mat, CV_32F);
			cv::multiply(float_mat, float_mat, squared_mat);
			cv::normalize(squared_mat, squared_mat, 0.0, 255.0, cv::NORM_MINMAX);
			squared_mat.convertTo(result, CV_8U);

			return result;
		},
			"BoostWhite"
		);
		return *this;
	}

	PreprocessingPipeline build() { return std::move(pipeline_); }

private:
	PreprocessingPipeline pipeline_;
};

PreprocessingPipeline buildPreprocessingPipeline(const std::vector<int>& preprocesses) {
	auto pipeLine = PreprocessingConstructor(PreprocessingPipeline::Mode::PARALLEL);
	for (int preprocId : preprocesses) {
		switch (preprocId)
		{
		case 0: pipeLine.original(); break;
		case 1: pipeLine.adaptiveThreshold(); break;
		case 2: pipeLine.unsharpMask(); break;
		case 3: pipeLine.adaptiveMean(); break;
		case 4: pipeLine.blurThreshold(); break;
		case 5: pipeLine.blurThreshold2(); break;
		case 6: pipeLine.scaledUnsharp(); break;
		case 7: pipeLine.otsuInverted(); break;
		case 8: pipeLine.histogramSharpening(); break;
		case 9: pipeLine.morphological(); break;
		case 10: pipeLine.blackEnhancement(); break;
		case 11: pipeLine.boostWhite(); break;
		default:
			break;
		}
	}
	return pipeLine.build();
}

std::unique_ptr<ZXing::Results> try_decode_image_crpt(const cv::Mat& image, const ZXing::DecodeHints& hints)
{
	std::unique_ptr<ZXing::Results> zxing_results = nullptr;

	if (hints.formats() == ZXing::BarcodeFormat::DataMatrix) {
		try {
			zxing_results = std::make_unique<ZXing::Results>(ZXing::readbarcodescrpt_samplegridv1(ZXing::ImageViewFromMat(image), hints));
		}
		catch (...) {
		}

	}
	else if (hints.hasFormat(ZXing::BarcodeFormat::DataMatrix)) {
		try {
			auto IV = ZXing::ImageViewFromMat(image);
			auto noDmHints = hints;
			noDmHints.setFormats(ZXing::BarcodeFormat::EAN13 | ZXing::BarcodeFormat::EAN8 | ZXing::BarcodeFormat::QRCode | ZXing::BarcodeFormat::PDF417);
			zxing_results = std::make_unique<ZXing::Results>(ZXing::readbarcodescrpt_samplegridv1(IV, hints));
			if (zxing_results == nullptr || zxing_results->size() < 1) {
				zxing_results = std::make_unique<ZXing::Results>(ZXing::ReadBarcodes(IV, noDmHints));
			}
		}
		catch (...) {
		}
	}
	else {
		try {
			zxing_results = std::make_unique<ZXing::Results>(ZXing::ReadBarcodes(ZXing::ImageViewFromMat(image), hints));
		}
		catch (...) {
		}

	}
	return zxing_results;
}

val jsObjFromResult(const ZXing::Result result) {
	auto ret = val::object();
	auto raw = result.bytesFNCFix();
	val rawMV = val(typed_memory_view(raw.size(), raw.data()));
	ret.set("type", ZXing::ToString(result.format()));
	ret.set("result", result.text());
	auto jsAr = val::global("Uint8Array").new_(raw.size());
	jsAr.call<void>("set", rawMV);
	ret.set("bytes", jsAr);
	return ret;
}

val matToJsImage(const cv::Mat& image) {
	std::vector<uint8_t> buf(image.total() * 4);
	cv::Mat rgbaImage = cv::Mat(image.rows, image.cols, CV_8UC4, buf.data());
	switch (image.channels()) {
	case 1: cv::cvtColor(image, rgbaImage, cv::COLOR_GRAY2RGBA); break;
	case 3: cv::cvtColor(image, rgbaImage, cv::COLOR_BGR2RGBA); break;
	case 4: cv::cvtColor(image, rgbaImage, cv::COLOR_BGRA2RGBA); break;
	}
	val imageMV = val(typed_memory_view(buf.size(), buf.data()));
	val retArr = val::global("Uint8ClampedArray").new_(buf.size());
	retArr.call<void>("set", imageMV);
	val imageData = val::global("ImageData").new_(retArr, double(rgbaImage.cols));
	return std::move(imageData);
}

val readCode(val jsTypedArray, int width, int height, val jsParams) {

	size_t length = jsTypedArray["length"].as<size_t>();

	std::vector<uint8_t> data(length);

	val memoryView = val(typed_memory_view(length, data.data()));

	memoryView.call<void>("set", jsTypedArray);

	bool tryUnwarp = jsParams["unwarp"].isUndefined() ? true : jsParams["unwarp"].as<bool>();
	bool onlyDM = jsParams["onlyDM"].isUndefined() ? false : jsParams["onlyDM"].as<bool>();
	bool debugPreprocData = jsParams["debugPreprocData"].isUndefined() ? false : jsParams["debugPreprocData"].as<bool>();
	bool debugPreprocImages = jsParams["debugPreprocImages"].isUndefined() ? false : jsParams["debugPreprocImages"].as<bool>();

	std::vector<int> preprocesses;
	if (jsParams["preprocesses"].isArray()) {
		val arg = jsParams["preprocesses"];
		size_t len = arg["length"].as<size_t>();
		preprocesses.reserve(len);
		for (int i = 0; i < len; i++) {
			if(arg[i].isNumber()) {
				preprocesses.push_back(arg[i].as<int>());
			}
		}
	}
	else {
		preprocesses = { 0, 1, 2, 3, 4, 5, 6 };
	}

	auto hints = ZXing::DecodeHints()
		.setFormats(onlyDM ? ZXing::BarcodeFormat::DataMatrix : (ZXing::BarcodeFormat::EAN13 | ZXing::BarcodeFormat::EAN8 | ZXing::BarcodeFormat::DataMatrix
			| ZXing::BarcodeFormat::QRCode | ZXing::BarcodeFormat::PDF417))
		.setTryRotate(true)
		.setTryDownscale(true)
		.setDownscaleFactor(4)
		.setBinarizer(ZXing::Binarizer::LocalAverage)
		.setIsPure(false)
		.setMaxNumberOfSymbols(0x1)
		.setEanAddOnSymbol(ZXing::EanAddOnSymbol::Ignore);

	if (data.size() % (width * height) != 0) {
		throw std::invalid_argument("Wrong array size");
	}
	int colorCnt = data.size() / (width * height);

	cv::Mat image_cv;
	switch (colorCnt)
	{
	case 1:
		image_cv = cv::Mat(height, width, CV_8UC1, data.data()).clone();
		break;
	case 3:
		cv::cvtColor(cv::Mat(height, width, CV_8UC3, data.data()), image_cv, cv::COLOR_RGB2BGR);
		break;
	case 4:
		cv::cvtColor(cv::Mat(height, width, CV_8UC4, data.data()), image_cv, cv::COLOR_RGBA2BGR);
		break;
	default:
		throw std::invalid_argument("Wrong array size");
	}

	data.clear();

	auto preprocPipeline = buildPreprocessingPipeline(preprocesses);
	ZXing::Result result;

	auto debugPreprocessesResults = val::array();
	bool isUnwarped = false;

	auto processImage = [&](const cv::Mat& unwrapped_image) -> bool {

		auto zxing_results_ptr = preprocPipeline.execute(unwrapped_image, hints, [&](const cv::Mat& image, const ZXing::DecodeHints& hints, const std::string& preprocName) {
			auto zxing_results = try_decode_image_crpt(image, hints);
			if (debugPreprocData) {
				val preprocResult = val::object();
				preprocResult.set("name", isUnwarped ? "unwarp + " + preprocName : preprocName);
				if (debugPreprocImages) {
					preprocResult.set("imageData", matToJsImage(image));
				}
				if (zxing_results != nullptr && zxing_results->size() > 0) {
					preprocResult.set("success", true);
				}
				else {
					preprocResult.set("success", false);
				}
				debugPreprocessesResults.call<void>("push", preprocResult);
			}
			return zxing_results;
		});

		if (zxing_results_ptr != nullptr && zxing_results_ptr->size() > 0) {
			result = (*zxing_results_ptr)[0];
			return true;
		}
		return false;
	};

	val jsResult = val::object();

	bool isAnyResults = processImage(image_cv);
	if (!isAnyResults && tryUnwarp) {
		cv::Mat unwarpedImage;
		isUnwarped = true;
		isAnyResults = cvUnwarpPreprocessPredefined(unwarpedImage, image_cv, {}, processImage, UnwarpParams());
	}
	if (isAnyResults) {
		jsResult.set("result", jsObjFromResult(result));
		if(debugPreprocData) {
			jsResult.set("debug", debugPreprocessesResults);
		}
		return jsResult;
	}

	jsResult.set("result", val::null());
	if(debugPreprocData) {
		jsResult.set("debug", debugPreprocessesResults);
	}
	return jsResult;
}



EMSCRIPTEN_BINDINGS(ZXingModule) {
	function("readCode", &readCode);
}
