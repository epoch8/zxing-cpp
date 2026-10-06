/*
* Copyright 2026 CRPT
*/
// SPDX-License-Identifier: Apache-2.0

#include "Preprocess.h"
#include "PreprocessGeometry.h"

#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

// Every op below is the body of the matching PreprocessingConstructor method
// in mediapipe's calculators/zxing/preprocessing_pipeline.h, copied verbatim:
// change one here and the calculator's local copy (still compiled for iOS)
// must change with it, or tools/compare_preprocessing_ops.sh B_IN_ZXING=1 and
// the wheel's CRPT_PREPROCESS_VERIFY mode will report the drift.

namespace ZXing {
namespace {

cv::Mat darkenImage_smart_ladder(const cv::Mat& image, int darken_amount)
{
	cv::Mat darkened;
	cv::convertScaleAbs(image, darkened, 1.0, -darken_amount);
	return darkened;
}

void grayscaleIfNecessary(const cv::Mat& img, cv::Mat& image_gr)
{
	switch (img.channels()) {
	case 1: image_gr = img.clone(); break;
	case 3: cv::cvtColor(img, image_gr, cv::COLOR_BGR2GRAY); break;
	case 4: cv::cvtColor(img, image_gr, cv::COLOR_BGRA2GRAY); break;
	default: throw std::invalid_argument("Unsupported image channels count");
	}
}

cv::Mat Original(const cv::Mat& img)
{
	return img.clone();
}

cv::Mat AdaptiveThreshold(const cv::Mat& img)
{
	cv::Mat image_gr, result;
	grayscaleIfNecessary(img, image_gr);
	cv::adaptiveThreshold(image_gr, result, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, 37, 2);
	return result;
}

cv::Mat UnsharpMask(const cv::Mat& img)
{
	cv::Mat blurred1, result, image_gr;
	grayscaleIfNecessary(img, image_gr);
	cv::GaussianBlur(image_gr, blurred1, cv::Size(23, 23), 0);
	cv::addWeighted(image_gr, 1.9, blurred1, -0.9, 0, result);
	return result;
}

cv::Mat AdaptiveMean(const cv::Mat& img)
{
	cv::Mat image_gr, result;
	grayscaleIfNecessary(img, image_gr);
	cv::adaptiveThreshold(image_gr, result, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, 17, 4);
	return result;
}

cv::Mat BlurThreshold(const cv::Mat& img)
{
	cv::Mat image_gr, blurred, image_mixed, result;
	grayscaleIfNecessary(img, image_gr);
	cv::GaussianBlur(image_gr, blurred, cv::Size(15, 15), 0);
	cv::addWeighted(image_gr, 1.8485371046459401, blurred, -0.8485371046459401, 0, image_mixed);
	cv::threshold(image_mixed, result, 100, 255, cv::THRESH_TRUNC);
	return result;
}

cv::Mat BlurThreshold2(const cv::Mat& img)
{
	cv::Mat image_gr, blurred, image_mixed, result;
	grayscaleIfNecessary(img, image_gr);
	cv::GaussianBlur(image_gr, blurred, cv::Size(45, 45), 0);
	cv::addWeighted(image_gr, 1.5037081148746935, blurred, -0.5037081148746935, 0, image_mixed);
	cv::threshold(image_mixed, result, 132, 255, 1);
	return result;
}

cv::Mat ScaledUnsharp(const cv::Mat& img)
{
	cv::Mat image_scaled, image_resized, blurred, image_gr, result;
	grayscaleIfNecessary(img, image_gr);
	cv::convertScaleAbs(image_gr, image_scaled, 0.7448063260333111, 30);
	// cv::INTER_LINEAR lands in resize's `fx` here, as in the calculator; with an
	// explicit dsize it is ignored and the interpolation stays the default.
	cv::resize(image_scaled, image_resized, cv::Size(int(img.cols * 2.1), int(img.rows * 2.1)), cv::INTER_LINEAR);
	cv::GaussianBlur(image_resized, blurred, cv::Size(49, 49), 0);
	cv::addWeighted(image_resized, 1.9705662110228968, blurred, -0.9705662110228968, 0, result);
	return result;
}

cv::Mat OtsuInverted(const cv::Mat& img)
{
	cv::Mat image_gr3, binary_image, result;
	grayscaleIfNecessary(img, image_gr3);
	cv::threshold(image_gr3, binary_image, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);
	cv::bitwise_not(binary_image, result);
	return result;
}

cv::Mat HistogramSharpening(const cv::Mat& img)
{
	cv::Mat darkened_image, darkened_image_gr, equalized, blurred2, result;
	cv::Mat sharpen_kernel;
	darkened_image = darkenImage_smart_ladder(img, 50);
	grayscaleIfNecessary(darkened_image, darkened_image_gr);
	cv::equalizeHist(darkened_image_gr, equalized);
	cv::GaussianBlur(equalized, blurred2, cv::Size(23, 23), 0);
	sharpen_kernel = (cv::Mat_<float>(3, 3) << -1, -1, -1, -1, 9, -1, -1, -1, -1);
	cv::filter2D(blurred2, result, -1, sharpen_kernel);
	return result;
}

cv::Mat Morphological(const cv::Mat& img)
{
	try {
		cv::Mat darkened_image, darkened_image_gr, blurred3, thresholded1, eroded, result;
		darkened_image = darkenImage_smart_ladder(img, 50);
		grayscaleIfNecessary(darkened_image, darkened_image_gr);
		cv::GaussianBlur(darkened_image_gr, blurred3, cv::Size(11, 11), 0);
		cv::threshold(blurred3, thresholded1, 0, 255, cv::THRESH_OTSU);
		if (thresholded1.empty() || thresholded1.depth() != CV_8U) {
			return cv::Mat();
		}
		cv::Mat kernel = cv::Mat::ones(3, 3, CV_8U);
		cv::erode(thresholded1, eroded, kernel, cv::Point(-1, -1), 1);
		cv::dilate(eroded, result, kernel, cv::Point(-1, -1), 1);
		return result;
	} catch (const cv::Exception& e) {
		return cv::Mat();
	} catch (...) {
		return cv::Mat();
	}
}

cv::Mat BlackEnhancement(const cv::Mat& img)
{
	cv::Mat img_gr, result, float_mat, squared_mat;
	grayscaleIfNecessary(img, img_gr);
	img_gr.convertTo(float_mat, CV_32F);
	cv::multiply(float_mat, float_mat, squared_mat);
	cv::normalize(squared_mat, squared_mat, 0.0, 255.0, cv::NORM_MINMAX);
	squared_mat.convertTo(result, CV_8U);
	return result;
}

cv::Mat BoostWhite(const cv::Mat& img)
{
	cv::Mat result, white, gamma_corrected;
	img.copyTo(result);
	img.copyTo(white);
	cv::normalize(white, white, 0.0, 360.0, cv::NORM_MINMAX);
	double inv_gamma = 1.0 / 0.9;
	cv::Mat lut(1, 256, CV_8U);
	uchar* p = lut.ptr();
	for (int i = 0; i < 256; ++i) {
		p[i] = cv::saturate_cast<uchar>(pow((double)i / 255.0, inv_gamma) * 255.0);
	}
	cv::LUT(white, lut, gamma_corrected);
	cv::addWeighted(result, 0.3, gamma_corrected, 0.7, 0.0, result);
	cv::normalize(result, result, 0.0, 255.0, cv::NORM_MINMAX);
	cv::Mat float_mat, squared_mat;
	result.convertTo(float_mat, CV_32F);
	cv::multiply(float_mat, float_mat, squared_mat);
	cv::normalize(squared_mat, squared_mat, 0.0, 255.0, cv::NORM_MINMAX);
	squared_mat.convertTo(result, CV_8U);
	return result;
}

cv::Mat Clahe(const cv::Mat& img)
{
	cv::Mat image_gr, clahe_applied, result;
	grayscaleIfNecessary(img, image_gr);
	auto clahe = cv::createCLAHE(2.0, {8, 8});
	clahe->apply(image_gr, clahe_applied);
	cv::adaptiveThreshold(clahe_applied, result, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, 37, 2);
	return result;
}

cv::Mat NormalizeEx(const cv::Mat& img)
{
	cv::Mat image_gr, result;
	grayscaleIfNecessary(img, image_gr);
	cv::normalize(image_gr, image_gr, -50.0, 305.0, cv::NORM_MINMAX);
	cv::adaptiveThreshold(image_gr, result, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, 37, 2);
	return result;
}

cv::Mat SauvolaBinarization(const cv::Mat& img, int window, double k, double R)
{
	cv::Mat gray, result;
	grayscaleIfNecessary(img, gray);
	cv::Mat float_img;
	gray.convertTo(float_img, CV_32F, 1.0 / 255.0);
	cv::Mat integral, integral_sq;
	cv::integral(float_img, integral, integral_sq, CV_32F);
	int w2 = window / 2;
	result = cv::Mat::zeros(gray.size(), CV_8U);
	for (int i = 0; i < gray.rows; i++) {
		for (int j = 0; j < gray.cols; j++) {
			int x1 = std::max(0, j - w2);
			int x2 = std::min(gray.cols - 1, j + w2);
			int y1 = std::max(0, i - w2);
			int y2 = std::min(gray.rows - 1, i + w2);
			int count = (x2 - x1 + 1) * (y2 - y1 + 1);
			float sum = integral.at<float>(y2 + 1, x2 + 1) - integral.at<float>(y1, x2 + 1) -
						integral.at<float>(y2 + 1, x1) + integral.at<float>(y1, x1);
			float sum_sq = integral_sq.at<float>(y2 + 1, x2 + 1) - integral_sq.at<float>(y1, x2 + 1) -
						   integral_sq.at<float>(y2 + 1, x1) + integral_sq.at<float>(y1, x1);
			float mean = sum / count;
			float variance = (sum_sq / count) - (mean * mean);
			float stddev = sqrtf(std::max(variance, 0.0f));
			float threshold = mean * (1.0 + k * (stddev / R - 1.0));
			result.at<uchar>(i, j) = (float_img.at<float>(i, j) > threshold) ? 255 : 0;
		}
	}
	return result;
}

cv::Mat ClaheEnhancement(const cv::Mat& img, double clipLimit, int gridSize)
{
	cv::Mat gray, result;
	grayscaleIfNecessary(img, gray);
	cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(clipLimit, cv::Size(gridSize, gridSize));
	clahe->apply(gray, result);
	return result;
}

cv::Mat TopHatTransformation(const cv::Mat& img, int kernelSize)
{
	cv::Mat gray, result;
	grayscaleIfNecessary(img, gray);
	cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kernelSize, kernelSize));
	cv::morphologyEx(gray, result, cv::MORPH_TOPHAT, kernel);
	return result;
}

cv::Mat BlackHatTransformation(const cv::Mat& img, int kernelSize)
{
	cv::Mat gray, result;
	grayscaleIfNecessary(img, gray);
	cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kernelSize, kernelSize));
	cv::morphologyEx(gray, result, cv::MORPH_BLACKHAT, kernel);
	return result;
}

cv::Mat AnisotropicDiffusion(const cv::Mat& img, int iterations, double lambda, double k)
{
	cv::Mat gray, result;
	grayscaleIfNecessary(img, gray);
	gray.convertTo(result, CV_32F, 1.0 / 255.0);
	for (int iter = 0; iter < iterations; iter++) {
		cv::Mat temp = result.clone();
		for (int i = 1; i < result.rows - 1; i++) {
			for (int j = 1; j < result.cols - 1; j++) {
				float gradN = temp.at<float>(i - 1, j) - temp.at<float>(i, j);
				float gradS = temp.at<float>(i + 1, j) - temp.at<float>(i, j);
				float gradE = temp.at<float>(i, j + 1) - temp.at<float>(i, j);
				float gradW = temp.at<float>(i, j - 1) - temp.at<float>(i, j);
				float cN = exp(-(gradN * gradN) / (k * k));
				float cS = exp(-(gradS * gradS) / (k * k));
				float cE = exp(-(gradE * gradE) / (k * k));
				float cW = exp(-(gradW * gradW) / (k * k));
				result.at<float>(i, j) += lambda * (cN * gradN + cS * gradS + cE * gradE + cW * gradW);
			}
		}
	}
	result.convertTo(result, CV_8U, 255.0);
	return result;
}

cv::Mat SuperResolution(const cv::Mat& img, double scale)
{
	cv::Mat result;
	cv::resize(img, result, cv::Size(int(img.cols * scale), int(img.rows * scale)), 0, 0, cv::INTER_LANCZOS4);
	return result;
}

cv::Mat Erode(const cv::Mat& img, bool isWhiteOnBlack, int erosion_size)
{
	cv::Mat image_gr, binary_img;
	grayscaleIfNecessary(img, image_gr);
	cv::adaptiveThreshold(image_gr, binary_img, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, 17, 4);
	if (isWhiteOnBlack) {
		binary_img = 255 - binary_img;
	}
	cv::Mat result;
	cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2 * erosion_size + 1, 2 * erosion_size + 1));
	cv::erode(binary_img, result, kernel);
	return result;
}

cv::Mat Rotate(const cv::Mat& img, double angle_degrees)
{
	cv::Point2f center(img.cols / 2.0f, img.rows / 2.0f);
	cv::Mat rot = cv::getRotationMatrix2D(center, angle_degrees, 1.0);
	cv::Mat result;
	cv::warpAffine(img, result, rot, img.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
	return result;
}

cv::Mat CenterCrop(const cv::Mat& img, double factor)
{
	int new_w = int(img.cols * factor);
	int new_h = int(img.rows * factor);
	int x = (img.cols - new_w) / 2;
	int y = (img.rows - new_h) / 2;
	if (new_w <= 0 || new_h <= 0)
		return cv::Mat();
	return img(cv::Rect(x, y, new_w, new_h)).clone();
}

cv::Mat Run(PreprocessOp op, const PreprocessParams& p, const cv::Mat& img)
{
	switch (op) {
	case PreprocessOp::Original: return Original(img);
	case PreprocessOp::AdaptiveThreshold: return AdaptiveThreshold(img);
	case PreprocessOp::UnsharpMask: return UnsharpMask(img);
	case PreprocessOp::AdaptiveMean: return AdaptiveMean(img);
	case PreprocessOp::BlurThreshold: return BlurThreshold(img);
	case PreprocessOp::BlurThreshold2: return BlurThreshold2(img);
	case PreprocessOp::ScaledUnsharp: return ScaledUnsharp(img);
	case PreprocessOp::OtsuInverted: return OtsuInverted(img);
	case PreprocessOp::HistogramSharpening: return HistogramSharpening(img);
	case PreprocessOp::Morphological: return Morphological(img);
	case PreprocessOp::BlackEnhancement: return BlackEnhancement(img);
	case PreprocessOp::BoostWhite: return BoostWhite(img);
	case PreprocessOp::CLAHE: return Clahe(img);
	case PreprocessOp::NormalizeEx: return NormalizeEx(img);
	case PreprocessOp::SauvolaBinarization: return SauvolaBinarization(img, p.window, p.sauvolaK, p.sauvolaR);
	case PreprocessOp::ClaheEnhancement: return ClaheEnhancement(img, p.clipLimit, p.gridSize);
	case PreprocessOp::TopHatTransformation: return TopHatTransformation(img, p.kernelSize);
	case PreprocessOp::BlackHatTransformation: return BlackHatTransformation(img, p.kernelSize);
	case PreprocessOp::AnisotropicDiffusion: return AnisotropicDiffusion(img, p.iterations, p.lambda, p.diffusionK);
	case PreprocessOp::SuperResolution: return SuperResolution(img, p.scale);
	case PreprocessOp::Erode: return Erode(img, p.whiteOnBlack, p.erosionSize);
	case PreprocessOp::Rotate: return Rotate(img, p.angleDegrees);
	case PreprocessOp::CenterCrop: return CenterCrop(img, p.factor);
	}
	return cv::Mat();
}

} // namespace

bool Preprocess(PreprocessOp op, const PreprocessParams& params, const ImageView& in, PreprocessImage& out) noexcept
{
	out = PreprocessImage();
	try {
		const int channels = in.pixStride();
		if (channels != 1 && channels != 3 && channels != 4)
			return false;
		// A view, not a copy: the ops read their input and allocate their output.
		const cv::Mat img(in.height(), in.width(), CV_8UC(channels), const_cast<uint8_t*>(in.data(0, 0)),
						  static_cast<size_t>(in.rowStride()));
		const cv::Mat result = Run(op, params, img);
		if (result.empty() || result.depth() != CV_8U)
			return false;
		out.width = result.cols;
		out.height = result.rows;
		out.channels = result.channels();
		const size_t rowBytes = static_cast<size_t>(result.cols) * result.elemSize();
		out.pixels.resize(rowBytes * result.rows);
		for (int r = 0; r < result.rows; ++r)
			std::memcpy(out.pixels.data() + r * rowBytes, result.ptr(r), rowBytes);
		return true;
	} catch (...) {
		out = PreprocessImage();
		return false;
	}
}

Affine PreprocessGeometry(PreprocessOp op, const PreprocessParams& p, int inW, int inH, int outW, int outH) noexcept
{
	switch (op) {
	case PreprocessOp::ScaledUnsharp:
	case PreprocessOp::SuperResolution: {
		if (inW <= 0 || inH <= 0)
			return {};
		// cv::resize maps pixel centres: x' + 0.5 = s (x + 0.5).
		const double sx = double(outW) / inW, sy = double(outH) / inH;
		return {{sx, 0, 0.5 * (sx - 1), 0, sy, 0.5 * (sy - 1)}};
	}
	case PreprocessOp::CenterCrop: {
		// The ROI's top-left, as CenterCrop computes it (integer division).
		const int x = (inW - outW) / 2, y = (inH - outH) / 2;
		return {{1, 0, -double(x), 0, 1, -double(y)}};
	}
	case PreprocessOp::Rotate: {
		// cv::getRotationMatrix2D(centre, angle, 1): the source-to-destination
		// matrix warpAffine applies (no WARP_INVERSE_MAP); centre in float, as there.
		const double cx = float(inW / 2.0f), cy = float(inH / 2.0f);
		const double a = p.angleDegrees * 3.14159265358979323846 / 180.0;
		const double alpha = std::cos(a), beta = std::sin(a);
		return {{alpha, beta, (1 - alpha) * cx - beta * cy, -beta, alpha, beta * cx + (1 - alpha) * cy}};
	}
	default:
		return {};
	}
}

} // namespace ZXing
