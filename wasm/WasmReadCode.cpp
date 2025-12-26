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

	try {
		zxing_results = std::make_unique<ZXing::Results>(
			ZXing::readbarcodescrpt_samplegridv1(ZXing::ImageViewFromMat(image), hints));
	}
	catch (...) {
		zxing_results = nullptr;
	}

	return zxing_results;
}


std::string readCode(val jsTypedArray, int width, int height) {

	uint8_t length = jsTypedArray["length"].as<uint8_t>();

	std::vector<uint8_t> data(length);

	val memoryView = val(typed_memory_view(length, data.data()));

	memoryView.call<void>("set", jsTypedArray);

	const auto hints = ZXing::DecodeHints()
		.setFormats(ZXing::BarcodeFormat::EAN13 | ZXing::BarcodeFormat::EAN8 | ZXing::BarcodeFormat::DataMatrix
					| ZXing::BarcodeFormat::QRCode | ZXing::BarcodeFormat::PDF417)
		.setTryRotate(true)
		.setTryDownscale(true)
		.setDownscaleFactor(4)
		.setBinarizer(ZXing::Binarizer::LocalAverage)
		.setIsPure(false)
		.setMaxNumberOfSymbols(0xff)
		.setEanAddOnSymbol(ZXing::EanAddOnSymbol::Ignore);

	auto image_cv = cv::Mat(height, width, CV_8UC3, data.data()).clone();
	data.clear();

	Preprocesses preprocessesState;

	ZXing::Result result;

	auto processImage = [&](const cv::Mat& unwrapped_image) -> bool {
		for (int candidate = 0; candidate <= 6; candidate++) {
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
	if (!anyResults) {
		try {
			cv::Mat unwarpedImage;
			anyResults = cvUnwarpPreprocessPredefined(unwarpedImage, image_cv, {}, processImage, UnwarpParams());
		} catch(...) {
		}
	}
	if (anyResults) {
		return result.text();
	}

	return "";
}

EMSCRIPTEN_BINDINGS(ZXingModule) {
	function("readCode", &readCode);
}
