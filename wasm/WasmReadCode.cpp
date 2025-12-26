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
		return {nullptr, 0, 0, ImageFormat::None}; // Use the fully qualified name

	return {image.data, image.cols, image.rows, fmt};
}

cv::Mat get_next_possible_image(cv::Mat image, int candidate) {
	cv::Mat image_candidate;
	cv::Mat image_mixed;
	cv::Mat blurred;
	cv::Mat image_scaled;
	cv::Mat image_resized;
	cv::Mat image_gr;
	cv::Mat image_gr2;

	cv::cvtColor(image, image_gr, cv::COLOR_BGR2GRAY);
	switch (candidate)
	{
	case 0:
		image_candidate = image.clone();

	case 1:
		cv::adaptiveThreshold(image_gr, image_candidate, 255, 0, 0, 37, 2);
		image_gr.release();
		break;

	case 2:
		cv::GaussianBlur(image, blurred, cv::Size(23, 23), 0);
		cv::addWeighted(image, 1.9, blurred, -0.9, 0, image_candidate);
		blurred.release();
		break;

	case 3:
		cv::adaptiveThreshold(image_gr, image_candidate, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, 17, 4);
		image_gr.release();
		break;

	case 4:
		cv::GaussianBlur(image, blurred, cv::Size(15,15), 0);
		cv::addWeighted(image, 1.8485371046459401, blurred, -0.8485371046459401, 0, image_mixed);
		blurred.release();
		cv::cvtColor(image_mixed, image_gr2, cv::COLOR_BGR2GRAY);
		image_mixed.release();
		cv::threshold(image_gr2, image_candidate, 100, 255, 2);
		image_gr2.release();
		break;

	case 5:
		cv::GaussianBlur(image, blurred, cv::Size(45,45), 0);
		cv::addWeighted(image, 1.5037081148746935, blurred, -0.5037081148746935, 0, image_mixed);
		blurred.release();
		cv::cvtColor(image_mixed, image_gr2, cv::COLOR_BGR2GRAY);
		image_mixed.release();
		cv::threshold(image_gr2, image_candidate, 132, 255, 1);
		image_gr2.release();
		break;


	case 6:
		cv::convertScaleAbs(image,image_scaled, 0.7448063260333111, 30);
		//int width = ;
		//int height = ;
		cv::resize(image_scaled, image_resized, cv::Size(int(image.cols * 2.1), int(image.rows * 2.1)), cv::INTER_LINEAR);
		image_scaled.release();
		cv::GaussianBlur(image_resized, blurred, cv::Size(49,49), 0);
		cv::addWeighted(image_resized, 1.9705662110228968, blurred, -0.9705662110228968, 0, image_mixed);
		image_resized.release();
		blurred.release();
		cv::cvtColor(image_mixed, image_candidate, cv::COLOR_BGR2GRAY);
		image_mixed.release();
		break;


	default:
		break;
	}
	return image_candidate;
}

std::unique_ptr<Results> try_decode_image_crpt(cv::Mat image_cv, cv::Mat image, const DecodeHints& hints)
{
	std::unique_ptr<Results> zxing_results = nullptr;

	try {
		zxing_results = std::make_unique<Results>(
			readbarcodescrpt_samplegridv1(ImageViewFromMat(image), hints));
	} catch (...) {
		zxing_results = nullptr;
	}

	return zxing_results;
}
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

		for (int candidate = 0; candidate <= 6; candidate++) {
			cv::Mat image_candidate = ZXing::get_next_possible_image(image_cv, candidate);
			std::unique_ptr<ZXing::Results> zxing_results_ptr = ZXing::try_decode_image_crpt(image_cv, image_candidate, hints);
			image_candidate.release();
			if (zxing_results_ptr != nullptr && zxing_results_ptr->size() >= 1) {
				const auto& result = (*zxing_results_ptr)[0];
				return result.text();
			}
		}
	return "";
}

EMSCRIPTEN_BINDINGS(ZXingModule) {
    function("readCode", &readCode);
}