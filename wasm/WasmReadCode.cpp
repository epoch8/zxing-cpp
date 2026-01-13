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
class Preprocesses {
	cv::Mat image_gr;
	cv::Mat buf1;
	cv::Mat buf2;
public:

	cv::Mat get_next_possible_image(const cv::Mat& image, int candidate) {


		switch (candidate)
		{
		case 0:
			return image.clone();
		case 1:
			if (image_gr.empty()) {
				cv::cvtColor(image, image_gr, cv::COLOR_BGR2GRAY);
			}
			cv::adaptiveThreshold(image_gr, buf1, 255, 0, 0, 37, 2);
			return buf1;
		case 2:
			cv::GaussianBlur(image, buf1, cv::Size(23, 23), 0);
			cv::addWeighted(image, 1.9, buf1, -0.9, 0, buf2);
			return buf2;
		case 3:
			if (image_gr.empty()) {
				cv::cvtColor(image, image_gr, cv::COLOR_BGR2GRAY);
			}
			cv::adaptiveThreshold(image_gr, buf1, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, 17, 4);
			return buf1;
		case 4:
			cv::GaussianBlur(image, buf1, cv::Size(15, 15), 0);
			cv::addWeighted(image, 1.8485371046459401, buf1, -0.8485371046459401, 0, buf2);
			cv::cvtColor(buf2, buf1, cv::COLOR_BGR2GRAY);
			cv::threshold(buf1, buf2, 100, 255, 2);
			return buf2;
		case 5:
			cv::GaussianBlur(image, buf1, cv::Size(45, 45), 0);
			cv::addWeighted(image, 1.5037081148746935, buf1, -0.5037081148746935, 0, buf2);
			cv::cvtColor(buf2, buf1, cv::COLOR_BGR2GRAY);
			cv::threshold(buf1, buf2, 132, 255, 1);
			return buf2;
		case 6:
			cv::convertScaleAbs(image, buf1, 0.7448063260333111, 30);
			cv::resize(buf1, buf2, cv::Size(int(image.cols * 2.1), int(image.rows * 2.1)), cv::INTER_LINEAR);
			cv::GaussianBlur(buf2, buf1, cv::Size(49, 49), 0);
			cv::addWeighted(buf2, 1.9705662110228968, buf1, -0.9705662110228968, 0, buf1);
			cv::cvtColor(buf1, buf2, cv::COLOR_BGR2GRAY);
			return buf2;
		default:
			break;
		}
		return {};
	}
};

std::unique_ptr<ZXing::Results> try_decode_image_crpt(cv::Mat image_cv, cv::Mat image, const ZXing::DecodeHints& hints)
{
	std::unique_ptr<ZXing::Results> zxing_results = nullptr;

	if(hints.formats() == ZXing::BarcodeFormat::DataMatrix) {
		try {
			zxing_results = std::make_unique<ZXing::Results>(ZXing::readbarcodescrpt_samplegridv1(ZXing::ImageViewFromMat(image), hints));
		}
		catch (...) {
		}

	}
	else if(hints.hasFormat(ZXing::BarcodeFormat::DataMatrix)) {
		try {
			auto IV = ZXing::ImageViewFromMat(image);
			auto noDmHints = hints;
			noDmHints.setFormats(ZXing::BarcodeFormat::EAN13 | ZXing::BarcodeFormat::EAN8 | ZXing::BarcodeFormat::QRCode | ZXing::BarcodeFormat::PDF417);
			zxing_results = std::make_unique<ZXing::Results>(ZXing::readbarcodescrpt_samplegridv1(IV, hints));
			if(zxing_results == nullptr || zxing_results->size() < 1) {
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
	auto bytes = result.bytesFNCFix();
	auto raw = result.bytesFNCFix();
	val rawMV = val(typed_memory_view(raw.size(), raw.data()));
	ret.set("type", ZXing::ToString(result.format()));
	ret.set("result", result.text());
	auto jsAr = val::global("Uint8Array").new_(raw.size());
	jsAr.call<void>("set", rawMV);
	ret.set("bytes", jsAr);
	return ret;
}

val readCode(val jsTypedArray, int width, int height, val jsParams) {

	size_t length = jsTypedArray["length"].as<size_t>();

	std::vector<uint8_t> data(length);

	val memoryView = val(typed_memory_view(length, data.data()));

	memoryView.call<void>("set", jsTypedArray);

	bool tryUnwarp = jsParams["unwarp"].isUndefined() ? true : jsParams["unwarp"].as<bool>();
	int preprocessesCount = jsParams["preproc"].isNumber() ? jsParams["preproc"].as<int>() : 6;
	bool onlyDM = jsParams["onlyDM"].isUndefined() ? false : jsParams["onlyDM"].as<bool>();
	preprocessesCount = preprocessesCount > 6 ? 6 : preprocessesCount;

	auto hints = ZXing::DecodeHints()
		.setFormats(onlyDM ? ZXing::BarcodeFormat::DataMatrix : ( ZXing::BarcodeFormat::EAN13 | ZXing::BarcodeFormat::EAN8 | ZXing::BarcodeFormat::DataMatrix
					| ZXing::BarcodeFormat::QRCode | ZXing::BarcodeFormat::PDF417))
		.setTryRotate(true)
		.setTryDownscale(true)
		.setDownscaleFactor(4)
		.setBinarizer(ZXing::Binarizer::LocalAverage)
		.setIsPure(false)
		.setMaxNumberOfSymbols(0x1)
		.setEanAddOnSymbol(ZXing::EanAddOnSymbol::Ignore);

	if(data.size() % (width * height) != 0) {
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

	Preprocesses preprocessesState;

	ZXing::Result result;

	auto processImage = [&](const cv::Mat& unwrapped_image) -> bool {
		for (int candidate = 0; candidate <= preprocessesCount; candidate++) {
			cv::Mat image_candidate = preprocessesState.get_next_possible_image(image_cv, candidate);
			std::unique_ptr<ZXing::Results> zxing_results_ptr = try_decode_image_crpt(image_cv, image_candidate, hints);
			image_candidate.release();
			if (zxing_results_ptr != nullptr && zxing_results_ptr->size() >= 1) {
				result = (*zxing_results_ptr)[0];
				return true;
			}
		}
		return false;
	};

	bool anyResults = processImage(image_cv);
	if (!anyResults && tryUnwarp) {
		cv::Mat unwarpedImage;
		anyResults = cvUnwarpPreprocessPredefined(unwarpedImage, image_cv, {}, processImage, UnwarpParams());
	}
	if (anyResults) {
		return jsObjFromResult(result);
	}

	return val::null();
}

EMSCRIPTEN_BINDINGS(ZXingModule) {
	function("readCode", &readCode);
}
