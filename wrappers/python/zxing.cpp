/*
 * Copyright 2019 Tim Rae
 * Copyright 2021 Antoine Humbert
 * Copyright 2021 Axel Waggershauser
 */
// SPDX-License-Identifier: Apache-2.0

#include "BarcodeFormat.h"

// Reader
#include "ReadBarcode.h"
#include "ZXAlgorithms.h"

// Writer
#include "BitMatrix.h"
#include "MultiFormatWriter.h"

// Add unwarp support
#include "unwarp_preprocess.h"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <optional>
#include <memory>
#include <vector>
#include <functional>

// Add OpenCV for unwarp strategies
#include <opencv2/opencv.hpp>

using namespace ZXing;
namespace py = pybind11;

// Numpy array wrapper class for images (either BGR or GRAYSCALE)
using Image = py::array_t<uint8_t, py::array::c_style>;

std::ostream& operator<<(std::ostream& os, const Position& points) {
	for (const auto& p : points)
		os << p.x << "x" << p.y << " ";
	os.seekp(-1, os.cur);
	os << '\0';
	return os;
}

auto read_barcodes_impl(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale, TextMode text_mode,
						Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol, uint8_t max_number_of_symbols = 0xff)
{
	const auto hints = DecodeHints()
		.setFormats(formats)
		.setTryRotate(try_rotate)
		.setTryDownscale(try_downscale)
		.setTextMode(text_mode)
		.setBinarizer(binarizer)
		.setIsPure(is_pure)
		.setMaxNumberOfSymbols(max_number_of_symbols)
		.setEanAddOnSymbol(ean_add_on_symbol);
	const auto _type = std::string(py::str(py::type::of(_image)));
	Image image;
	try {
		image = _image.cast<Image>();
	}
	catch(...) {
		throw py::type_error("Unsupported type " + _type + ". Expect a PIL Image or numpy array");
	}
	const auto height = narrow_cast<int>(image.shape(0));
	const auto width = narrow_cast<int>(image.shape(1));
	auto channels = image.ndim() == 2 ? 1 : narrow_cast<int>(image.shape(2));
	ImageFormat imgfmt;
	if (_type.find("PIL.") != std::string::npos) {
		const auto mode = _image.attr("mode").cast<std::string>();
		if (mode == "L")
			imgfmt = ImageFormat::Lum;
		else if (mode == "RGB")
			imgfmt = ImageFormat::RGB;
		else if (mode == "RGBA")
			imgfmt = ImageFormat::RGBX;
		else {
			// Unsupported mode in ImageFormat. Let's do conversion to L mode with PIL
			image = _image.attr("convert")("L").cast<Image>();
			imgfmt = ImageFormat::Lum;
			channels = 1;
		}
	} else {
		// Assume grayscale or BGR image depending on channels number
		if (channels == 1)
			imgfmt = ImageFormat::Lum;
		else if (channels == 3)
			imgfmt = ImageFormat::BGR;
		else
			throw py::type_error("Unsupported number of channels for numpy array: " + std::to_string(channels));
	}

	const auto bytes = image.data();
	return ReadBarcodes({bytes, width, height, imgfmt, width * channels, channels}, hints);
}

// SampleGrid strategy implementation
auto read_barcodes_samplegrid_impl(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale, TextMode text_mode,
								  Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol, uint8_t max_number_of_symbols = 0xff)
{
	const auto hints = DecodeHints()
		.setFormats(formats)
		.setTryRotate(try_rotate)
		.setTryDownscale(try_downscale)
		.setTextMode(text_mode)
		.setBinarizer(binarizer)
		.setIsPure(is_pure)
		.setMaxNumberOfSymbols(max_number_of_symbols)
		.setEanAddOnSymbol(ean_add_on_symbol);
	const auto _type = std::string(py::str(py::type::of(_image)));
	Image image;
	try {
		image = _image.cast<Image>();
	}
	catch(...) {
		throw py::type_error("Unsupported type " + _type + ". Expect a PIL Image or numpy array");
	}
	const auto height = narrow_cast<int>(image.shape(0));
	const auto width = narrow_cast<int>(image.shape(1));
	auto channels = image.ndim() == 2 ? 1 : narrow_cast<int>(image.shape(2));
	ImageFormat imgfmt;
	if (_type.find("PIL.") != std::string::npos) {
		const auto mode = _image.attr("mode").cast<std::string>();
		if (mode == "L")
			imgfmt = ImageFormat::Lum;
		else if (mode == "RGB")
			imgfmt = ImageFormat::RGB;
		else if (mode == "RGBA")
			imgfmt = ImageFormat::RGBX;
		else {
			// Unsupported mode in ImageFormat. Let's do conversion to L mode with PIL
			image = _image.attr("convert")("L").cast<Image>();
			imgfmt = ImageFormat::Lum;
			channels = 1;
		}
	} else {
		// Assume grayscale or BGR image depending on channels number
		if (channels == 1)
			imgfmt = ImageFormat::Lum;
		else if (channels == 3)
			imgfmt = ImageFormat::BGR;
		else
			throw py::type_error("Unsupported number of channels for numpy array: " + std::to_string(channels));
	}

	const auto bytes = image.data();
	return readbarcodescrpt_samplegridv1({bytes, width, height, imgfmt, width * channels, channels}, hints);
}

// Helper function to convert numpy/PIL image to cv::Mat
cv::Mat imageToMat(py::object _image) {
	const auto _type = std::string(py::str(py::type::of(_image)));
	Image image;
	try {
		image = _image.cast<Image>();
	}
	catch(...) {
		throw py::type_error("Unsupported type " + _type + ". Expect a PIL Image or numpy array");
	}
	
	const auto height = narrow_cast<int>(image.shape(0));
	const auto width = narrow_cast<int>(image.shape(1));
	auto channels = image.ndim() == 2 ? 1 : narrow_cast<int>(image.shape(2));
	
	cv::Mat mat;
	if (channels == 1) {
		mat = cv::Mat(height, width, CV_8UC1, (void*)image.data());
	} else if (channels == 3) {
		mat = cv::Mat(height, width, CV_8UC3, (void*)image.data());
	} else {
		throw py::type_error("Unsupported number of channels for numpy array: " + std::to_string(channels));
	}
	
	return mat.clone(); // Clone to ensure data ownership
}

// Helper function to convert cv::Mat to ZXing ImageView
ZXing::ImageView matToImageView(const cv::Mat& mat) {
	using ZXing::ImageFormat;
	
	auto fmt = ImageFormat::None;
	switch (mat.channels()) {
		case 1: fmt = ImageFormat::Lum; break;
		case 3: fmt = ImageFormat::BGR; break;
		case 4: fmt = ImageFormat::BGRX; break;
	}
	
	if (mat.depth() != CV_8U || fmt == ImageFormat::None)
		return {nullptr, 0, 0, ImageFormat::None};
	
	return {mat.data, mat.cols, mat.rows, fmt};
}

// Unwarp + SampleGrid strategy implementation
auto read_barcodes_unwrap_samplegrid_impl(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale, TextMode text_mode,
										  Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol, uint8_t max_number_of_symbols = 0xff)
{
	const auto hints = DecodeHints()
		.setFormats(formats)
		.setTryRotate(try_rotate)
		.setTryDownscale(try_downscale)
		.setTextMode(text_mode)
		.setBinarizer(binarizer)
		.setIsPure(is_pure)
		.setMaxNumberOfSymbols(max_number_of_symbols)
		.setEanAddOnSymbol(ean_add_on_symbol);
	
	cv::Mat input_mat = imageToMat(_image);
	
	// Set up unwarp parameters (simplified from smart ladder calculator)
	UnwarpParams unwarpParams;
	unwarpParams.outputSize = 160;
	unwarpParams.offset = 15;
	float scale = 1.0 / 36.0;
	
	// Create warp variants (from smart ladder calculator)
	std::vector<std::vector<float>> vx1 = {
		{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
		{0, 0.42005197160798996, 0.7962254169255832, 0.9531493664624343, 1, 0.9531493664624343, 0.7962254169255832, 0.42005197160798996, 0},
		{-0, -0.42005197160798996, -0.7962254169255832, -0.9531493664624343, -1, -0.9531493664624343, -0.7962254169255832, -0.42005197160798996, -0}
	};
	
	std::vector<cv::Mat> warps;
	for(auto& w : vx1) {
		warps.push_back(cv::Mat_<float>(w.size(), 1, w.data()));
		warps.back() *= scale;
		resizeWarp(warps.back(), warps.back(), unwarpParams);
	}
	
	std::vector<std::pair<cv::Mat, cv::Mat>> warp_variants;
	for(const auto& [wx,wy] : (uint8_t[][2]){{0,1},{0,2},{1,0},{2,0}}) {
		warp_variants.emplace_back(warps[wx], warps[wy]);
	}
	
	Results result;
	
	// Define processImage lambda for SampleGrid strategy
	auto processImage = [&](const cv::Mat& unwrapped_image) -> bool {
		try {
			// Try samplegridv1 first
			auto temp_results = readbarcodescrpt_samplegridv1(matToImageView(unwrapped_image), hints);
			if (!temp_results.empty()) {
				result = std::move(temp_results);
				return true;
			}
			// Fallback to regular ZXing
			temp_results = ReadBarcodes(matToImageView(unwrapped_image), hints);
			if (!temp_results.empty()) {
				result = std::move(temp_results);
				return true;
			}
		} catch (...) {
			// Continue trying
		}
		return false;
	};
	
	cv::Mat unwarpedImage;
	bool success = cvUnwarpPreprocessPredefined(unwarpedImage, input_mat, warp_variants, processImage, unwarpParams);
	
	return result;
}

// Unwarp + Standard strategy implementation  
auto read_barcodes_unwrap_standard_impl(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale, TextMode text_mode,
									   Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol, uint8_t max_number_of_symbols = 0xff)
{
	const auto hints = DecodeHints()
		.setFormats(formats)
		.setTryRotate(try_rotate)
		.setTryDownscale(try_downscale)
		.setTextMode(text_mode)
		.setBinarizer(binarizer)
		.setIsPure(is_pure)
		.setMaxNumberOfSymbols(max_number_of_symbols)
		.setEanAddOnSymbol(ean_add_on_symbol);
	
	cv::Mat input_mat = imageToMat(_image);
	
	// Set up unwarp parameters (simplified from smart ladder calculator)
	UnwarpParams unwarpParams;
	unwarpParams.outputSize = 160;
	unwarpParams.offset = 15;
	float scale = 1.0 / 36.0;
	
	// Create warp variants (from smart ladder calculator)
	std::vector<std::vector<float>> vx1 = {
		{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
		{0, 0.42005197160798996, 0.7962254169255832, 0.9531493664624343, 1, 0.9531493664624343, 0.7962254169255832, 0.42005197160798996, 0},
		{-0, -0.42005197160798996, -0.7962254169255832, -0.9531493664624343, -1, -0.9531493664624343, -0.7962254169255832, -0.42005197160798996, -0}
	};
	
	std::vector<cv::Mat> warps;
	for(auto& w : vx1) {
		warps.push_back(cv::Mat_<float>(w.size(), 1, w.data()));
		warps.back() *= scale;
		resizeWarp(warps.back(), warps.back(), unwarpParams);
	}
	
	std::vector<std::pair<cv::Mat, cv::Mat>> warp_variants;
	for(const auto& [wx,wy] : (uint8_t[][2]){{0,1},{0,2},{1,0},{2,0}}) {
		warp_variants.emplace_back(warps[wx], warps[wy]);
	}
	
	Results result;
	
	// Define processImage lambda for Standard strategy (regular ZXing only)
	auto processImage = [&](const cv::Mat& unwrapped_image) -> bool {
		try {
			auto temp_results = ReadBarcodes(matToImageView(unwrapped_image), hints);
			if (!temp_results.empty()) {
				result = std::move(temp_results);
				return true;
			}
		} catch (...) {
			// Continue trying
		}
		return false;
	};
	
	cv::Mat unwarpedImage;
	bool success = cvUnwarpPreprocessPredefined(unwarpedImage, input_mat, warp_variants, processImage, unwarpParams);
	
	return result;
}

std::optional<Result> read_barcode(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale,
								   TextMode text_mode, Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol)
{
	auto res = read_barcodes_impl(_image, formats, try_rotate, try_downscale, text_mode, binarizer, is_pure, ean_add_on_symbol, 1);
	return res.empty() ? std::nullopt : std::optional(res.front());
}

Results read_barcodes(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale,
					  TextMode text_mode, Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol)
{
	return read_barcodes_impl(_image, formats, try_rotate, try_downscale, text_mode, binarizer, is_pure, ean_add_on_symbol);
}

// SampleGrid strategy functions
std::optional<Result> read_barcode_samplegrid(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale,
											 TextMode text_mode, Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol)
{
	auto res = read_barcodes_samplegrid_impl(_image, formats, try_rotate, try_downscale, text_mode, binarizer, is_pure, ean_add_on_symbol, 1);
	return res.empty() ? std::nullopt : std::optional(res.front());
}

Results read_barcodes_samplegrid(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale,
								TextMode text_mode, Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol)
{
	return read_barcodes_samplegrid_impl(_image, formats, try_rotate, try_downscale, text_mode, binarizer, is_pure, ean_add_on_symbol);
}

// Unwarp + SampleGrid strategy functions
std::optional<Result> read_barcode_unwrap_samplegrid(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale,
													TextMode text_mode, Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol)
{
	auto res = read_barcodes_unwrap_samplegrid_impl(_image, formats, try_rotate, try_downscale, text_mode, binarizer, is_pure, ean_add_on_symbol, 1);
	return res.empty() ? std::nullopt : std::optional(res.front());
}

Results read_barcodes_unwrap_samplegrid(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale,
									   TextMode text_mode, Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol)
{
	return read_barcodes_unwrap_samplegrid_impl(_image, formats, try_rotate, try_downscale, text_mode, binarizer, is_pure, ean_add_on_symbol);
}

// Unwarp + Standard strategy functions
std::optional<Result> read_barcode_unwrap_standard(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale,
												  TextMode text_mode, Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol)
{
	auto res = read_barcodes_unwrap_standard_impl(_image, formats, try_rotate, try_downscale, text_mode, binarizer, is_pure, ean_add_on_symbol, 1);
	return res.empty() ? std::nullopt : std::optional(res.front());
}

Results read_barcodes_unwrap_standard(py::object _image, const BarcodeFormats& formats, bool try_rotate, bool try_downscale,
									 TextMode text_mode, Binarizer binarizer, bool is_pure, EanAddOnSymbol ean_add_on_symbol)
{
	return read_barcodes_unwrap_standard_impl(_image, formats, try_rotate, try_downscale, text_mode, binarizer, is_pure, ean_add_on_symbol);
}

Image write_barcode(BarcodeFormat format, std::string text, int width, int height, int quiet_zone, int ec_level)
{
	auto writer = MultiFormatWriter(format).setEncoding(CharacterSet::UTF8).setMargin(quiet_zone).setEccLevel(ec_level);
	auto bitmap = writer.encode(text, width, height);

	auto result = Image({bitmap.height(), bitmap.width()});
	auto r = result.mutable_unchecked<2>();
	for (py::ssize_t y = 0; y < r.shape(0); y++)
		for (py::ssize_t x = 0; x < r.shape(1); x++)
			r(y, x) = bitmap.get(narrow_cast<int>(x), narrow_cast<int>(y)) ? 0 : 255;
	return result;
}

PYBIND11_MODULE(zxingcpp, m) {
	m.doc() = "python bindings for zxing-cpp";
	// m.attr("__version__") = VERSION_INFO;

	// Register types first (required for default arguments)
	py::enum_<BarcodeFormat>(m, "BarcodeFormat")
		.value("NONE", BarcodeFormat::None)
		.value("AZTEC", BarcodeFormat::Aztec)
		.value("CODABAR", BarcodeFormat::Codabar)
		.value("CODE_39", BarcodeFormat::Code39)
		.value("CODE_93", BarcodeFormat::Code93)
		.value("CODE_128", BarcodeFormat::Code128)
		.value("DATA_MATRIX", BarcodeFormat::DataMatrix)
		.value("EAN_8", BarcodeFormat::EAN8)
		.value("EAN_13", BarcodeFormat::EAN13)
		.value("ITF", BarcodeFormat::ITF)
		.value("MAXICODE", BarcodeFormat::MaxiCode)
		.value("PDF_417", BarcodeFormat::PDF417)
		.value("QR_CODE", BarcodeFormat::QRCode)
		.value("RSS_14", BarcodeFormat::DataBar)
		.value("RSS_EXPANDED", BarcodeFormat::DataBarExpanded)
		.value("UPC_A", BarcodeFormat::UPCA)
		.value("UPC_E", BarcodeFormat::UPCE)
		.value("ONE_D", BarcodeFormat::LinearCodes)
		.value("TWO_D", BarcodeFormat::MatrixCodes)
		;

	py::class_<BarcodeFormats>(m, "BarcodeFormats")
		.def(py::init<BarcodeFormat>())
		.def(py::init<>())
		.def("__len__", [](const BarcodeFormats& bf) { return bf.count(); })
		.def("__iter__", [](const BarcodeFormats &bfs) { return py::make_iterator(bfs.begin(), bfs.end()); }, py::keep_alive<0, 1>())
		.def("__contains__", [](const BarcodeFormats& bfs, BarcodeFormat f) { return bfs.testFlag(f); })
		.def("__ior__", [](BarcodeFormats& bfs, BarcodeFormat f) { return bfs |= f; })
		.def("clear", &BarcodeFormats::clear)
		.def("empty", &BarcodeFormats::empty)
		.def("__bool__", [](const BarcodeFormats& bfs) { return !bfs.empty(); })
		;

	py::enum_<Binarizer>(m, "Binarizer")
		.value("LOCAL_AVERAGE", Binarizer::LocalAverage)
		.value("GLOBAL_HISTOGRAM", Binarizer::GlobalHistogram)
		.value("FIXED_THRESHOLD", Binarizer::FixedThreshold)
		.value("BOOL_CAST", Binarizer::BoolCast)
		;

	py::enum_<EanAddOnSymbol>(m, "EanAddOnSymbol")
		.value("IGNORE", EanAddOnSymbol::Ignore)
		.value("READ", EanAddOnSymbol::Read)
		.value("REQUIRE", EanAddOnSymbol::Require)
		;

	py::enum_<TextMode>(m, "TextMode")
		.value("PLAIN", TextMode::Plain)
		.value("ECI", TextMode::ECI)
		.value("HRI", TextMode::HRI)
		.value("HEX", TextMode::Hex)
		.value("ESCAPED", TextMode::Escaped)
		;

	py::class_<PointI>(m, "Point")
		.def_readonly("x", &PointI::x)
		.def_readonly("y", &PointI::y)
		.def("__str__", [](const PointI& p) { return std::to_string(p.x) + "x" + std::to_string(p.y); })
		;

	py::class_<Position>(m, "Position")
		.def_property_readonly("top_left", [](const Position& p) { return p.topLeft(); })
		.def_property_readonly("top_right", [](const Position& p) { return p.topRight(); })
		.def_property_readonly("bottom_left", [](const Position& p) { return p.bottomLeft(); })
		.def_property_readonly("bottom_right", [](const Position& p) { return p.bottomRight(); })
		.def("__str__", [](const Position& p) {
			std::ostringstream oss;
			oss << p;
			return oss.str();
		})
		;

	py::class_<Result>(m, "Result")
		.def_property_readonly("valid", [](const Result& r) { return r.isValid(); })
		.def_property_readonly("text", [](const Result& r) { return r.text(); })
		.def_property_readonly("format", &Result::format)
		.def_property_readonly("symbology_identifier", &Result::symbologyIdentifier)
		.def_property_readonly("position", &Result::position)
		.def_property_readonly("orientation", &Result::orientation)
		.def_property_readonly("is_inverted", &Result::isInverted)
		.def_property_readonly("is_mirrored", &Result::isMirrored)
		.def_property_readonly("content_type", &Result::contentType)
		.def("__str__", [](const Result& r) { return r.text(); })
		.def("__bool__", [](const Result& r) { return r.isValid(); })
		;

#ifdef ZXING_BUILD_READERS
	m.def("read_barcode", &read_barcode,
		py::arg("image"),
		py::arg("formats") = BarcodeFormats{},
		py::arg("try_rotate") = true,
		py::arg("try_downscale") = true,
		py::arg("text_mode") = TextMode::HRI,
		py::arg("binarizer") = Binarizer::LocalAverage,
		py::arg("is_pure") = false,
		py::arg("ean_add_on_symbol") = EanAddOnSymbol::Ignore,
		"Read (decode) a barcode from a numpy BGR or grayscale image array or from a PIL image.\n\n"
		":type image: numpy.ndarray|PIL.Image.Image\n"
		":param image: The image object to decode. The image can be either:\n"
		"  - a numpy array containing image either in grayscale (1 byte per pixel) or BGR mode (3 bytes per pixel)\n"
		"  - a PIL Image\n"
		":type formats: zxing.BarcodeFormat|zxing.BarcodeFormats\n"
		":param formats: the format(s) to decode. If ``None``, decode all formats.\n"
		":type try_rotate: bool\n"
		":param try_rotate: if ``True`` (the default), decoder searches for barcodes in any direction; \n"
		"  if ``False``, it will not search for 90° / 270° rotated barcodes.\n"
		":type try_downscale: bool\n"
		":param try_downscale: if ``True`` (the default), decoder also scans downscaled versions of the input; \n"
		"  if ``False``, it will only search in the resolution provided.\n"
		":type text_mode: zxing.TextMode\n"
		":param text_mode: specifies the TextMode that governs how the raw bytes content is transcoded to text in the Result.\n"
		"  Defaults to :py:attr:`zxing.TextMode.HRI`."
		":type binarizer: zxing.Binarizer\n"
		":param binarizer: the binarizer used to convert image before decoding barcodes.\n"
		"  Defaults to :py:attr:`zxing.Binarizer.LocalAverage`."
		":type is_pure: bool\n"
		":param is_pure: Set to True if the input contains nothing but a perfectly aligned barcode (generated image).\n"
		"  Speeds up detection in that case. Default is False."
		":type ean_add_on_symbol: zxing.EanAddOnSymbol\n"
		":param ean_add_on_symbol: Specify whether to Ignore, Read or Require EAN-2/5 add-on symbols while scanning \n"
		"  EAN/UPC codes. Default is ``Ignore``.\n"
		":rtype: zxing.Result\n"
		":return: a zxing result containing decoded symbol if found, None otherwise"
		);
	m.def("read_barcodes", &read_barcodes,
		py::arg("image"),
		py::arg("formats") = BarcodeFormats{},
		py::arg("try_rotate") = true,
		py::arg("try_downscale") = true,
		py::arg("text_mode") = TextMode::HRI,
		py::arg("binarizer") = Binarizer::LocalAverage,
		py::arg("is_pure") = false,
		py::arg("ean_add_on_symbol") = EanAddOnSymbol::Ignore,
		"Read (decode) multiple barcodes from a numpy BGR or grayscale image array or from a PIL image.\n\n"
		":type image: numpy.ndarray|PIL.Image.Image\n"
		":param image: The image object to decode. The image can be either:\n"
		"  - a numpy array containing image either in grayscale (1 byte per pixel) or BGR mode (3 bytes per pixel)\n"
		"  - a PIL Image\n"
		":type formats: zxing.BarcodeFormat|zxing.BarcodeFormats\n"
		":param formats: the format(s) to decode. If ``None``, decode all formats.\n"
		":type try_rotate: bool\n"
		":param try_rotate: if ``True`` (the default), decoder searches for barcodes in any direction; \n"
		"  if ``False``, it will not search for 90° / 270° rotated barcodes.\n"
		":type try_downscale: bool\n"
		":param try_downscale: if ``True`` (the default), decoder also scans downscaled versions of the input; \n"
		"  if ``False``, it will only search in the resolution provided.\n"
		":type text_mode: zxing.TextMode\n"
		":param text_mode: specifies the TextMode that governs how the raw bytes content is transcoded to text in the Result.\n"
		"  Defaults to :py:attr:`zxing.TextMode.HRI`."
		":type binarizer: zxing.Binarizer\n"
		":param binarizer: the binarizer used to convert image before decoding barcodes.\n"
		"  Defaults to :py:attr:`zxing.Binarizer.LocalAverage`."
		":type is_pure: bool\n"
		":param is_pure: Set to True if the input contains nothing but a perfectly aligned barcode (generated image).\n"
		"  Speeds up detection in that case. Default is False."
		":type ean_add_on_symbol: zxing.EanAddOnSymbol\n"
		":param ean_add_on_symbol: Specify whether to Ignore, Read or Require EAN-2/5 add-on symbols while scanning \n"
		"  EAN/UPC codes. Default is ``Ignore``.\n"
		":rtype: zxing.Result\n"
		":return: a list of zxing results containing decoded symbols, the list is empty if none is found"
		);

	// SampleGrid strategy functions
	m.def("read_barcode_samplegrid", &read_barcode_samplegrid,
		py::arg("image"),
		py::arg("formats") = BarcodeFormats{},
		py::arg("try_rotate") = true,
		py::arg("try_downscale") = true,
		py::arg("text_mode") = TextMode::HRI,
		py::arg("binarizer") = Binarizer::LocalAverage,
		py::arg("is_pure") = false,
		py::arg("ean_add_on_symbol") = EanAddOnSymbol::Ignore,
		"Read (decode) a barcode using SampleGrid strategy from a numpy BGR or grayscale image array or from a PIL image.\n\n"
		"Uses the SampleGridV1 detection strategy which samples the image in a grid pattern.\n\n"
		":type image: numpy.ndarray|PIL.Image.Image\n"
		":param image: The image object to decode. The image can be either:\n"
		"  - a numpy array containing image either in grayscale (1 byte per pixel) or BGR mode (3 bytes per pixel)\n"
		"  - a PIL Image\n"
		":type formats: zxing.BarcodeFormat|zxing.BarcodeFormats\n"
		":param formats: the format(s) to decode. If ``None``, decode all formats.\n"
		":type try_rotate: bool\n"
		":param try_rotate: if ``True`` (the default), decoder searches for barcodes in any direction; \n"
		"  if ``False``, it will not search for 90° / 270° rotated barcodes.\n"
		":type try_downscale: bool\n"
		":param try_downscale: if ``True`` (the default), decoder also scans downscaled versions of the input; \n"
		"  if ``False``, it will only search in the resolution provided.\n"
		":type text_mode: zxing.TextMode\n"
		":param text_mode: specifies the TextMode that governs how the raw bytes content is transcoded to text in the Result.\n"
		"  Defaults to :py:attr:`zxing.TextMode.HRI`."
		":type binarizer: zxing.Binarizer\n"
		":param binarizer: the binarizer used to convert image before decoding barcodes.\n"
		"  Defaults to :py:attr:`zxing.Binarizer.LocalAverage`."
		":type is_pure: bool\n"
		":param is_pure: Set to True if the input contains nothing but a perfectly aligned barcode (generated image).\n"
		"  Speeds up detection in that case. Default is False."
		":type ean_add_on_symbol: zxing.EanAddOnSymbol\n"
		":param ean_add_on_symbol: Specify whether to Ignore, Read or Require EAN-2/5 add-on symbols while scanning \n"
		"  EAN/UPC codes. Default is ``Ignore``.\n"
		":rtype: zxing.Result\n"
		":return: a zxing result containing decoded symbol if found, None otherwise"
	);
	m.def("read_barcodes_samplegrid", &read_barcodes_samplegrid,
		py::arg("image"),
		py::arg("formats") = BarcodeFormats{},
		py::arg("try_rotate") = true,
		py::arg("try_downscale") = true,
		py::arg("text_mode") = TextMode::HRI,
		py::arg("binarizer") = Binarizer::LocalAverage,
		py::arg("is_pure") = false,
		py::arg("ean_add_on_symbol") = EanAddOnSymbol::Ignore,
		"Read (decode) multiple barcodes using SampleGrid strategy from a numpy BGR or grayscale image array or from a PIL image.\n\n"
		"Uses the SampleGridV1 detection strategy which samples the image in a grid pattern.\n\n"
		":type image: numpy.ndarray|PIL.Image.Image\n"
		":param image: The image object to decode. The image can be either:\n"
		"  - a numpy array containing image either in grayscale (1 byte per pixel) or BGR mode (3 bytes per pixel)\n"
		"  - a PIL Image\n"
		":type formats: zxing.BarcodeFormat|zxing.BarcodeFormats\n"
		":param formats: the format(s) to decode. If ``None``, decode all formats.\n"
		":type try_rotate: bool\n"
		":param try_rotate: if ``True`` (the default), decoder searches for barcodes in any direction; \n"
		"  if ``False``, it will not search for 90° / 270° rotated barcodes.\n"
		":type try_downscale: bool\n"
		":param try_downscale: if ``True`` (the default), decoder also scans downscaled versions of the input; \n"
		"  if ``False``, it will only search in the resolution provided.\n"
		":type text_mode: zxing.TextMode\n"
		":param text_mode: specifies the TextMode that governs how the raw bytes content is transcoded to text in the Result.\n"
		"  Defaults to :py:attr:`zxing.TextMode.HRI`."
		":type binarizer: zxing.Binarizer\n"
		":param binarizer: the binarizer used to convert image before decoding barcodes.\n"
		"  Defaults to :py:attr:`zxing.Binarizer.LocalAverage`."
		":type is_pure: bool\n"
		":param is_pure: Set to True if the input contains nothing but a perfectly aligned barcode (generated image).\n"
		"  Speeds up detection in that case. Default is False."
		":type ean_add_on_symbol: zxing.EanAddOnSymbol\n"
		":param ean_add_on_symbol: Specify whether to Ignore, Read or Require EAN-2/5 add-on symbols while scanning \n"
		"  EAN/UPC codes. Default is ``Ignore``.\n"
		":rtype: zxing.Result\n"
		":return: a list of zxing results containing decoded symbols, the list is empty if none is found"
		);

	// Unwrap + SampleGrid strategy functions
	m.def("read_barcode_unwrap_samplegrid", &read_barcode_unwrap_samplegrid,
		py::arg("image"),
		py::arg("formats") = BarcodeFormats{},
		py::arg("try_rotate") = true,
		py::arg("try_downscale") = true,
		py::arg("text_mode") = TextMode::HRI,
		py::arg("binarizer") = Binarizer::LocalAverage,
		py::arg("is_pure") = false,
		py::arg("ean_add_on_symbol") = EanAddOnSymbol::Ignore,
		"Read (decode) a barcode using Unwarp + SampleGrid strategy from a numpy BGR or grayscale image array or from a PIL image.\n\n"
		"Uses unwarp preprocessing with SampleGridV1 detection strategy for distorted or warped images.\n\n"
		":type image: numpy.ndarray|PIL.Image.Image\n"
		":param image: The image object to decode. The image can be either:\n"
		"  - a numpy array containing image either in grayscale (1 byte per pixel) or BGR mode (3 bytes per pixel)\n"
		"  - a PIL Image\n"
		":type formats: zxing.BarcodeFormat|zxing.BarcodeFormats\n"
		":param formats: the format(s) to decode. If ``None``, decode all formats.\n"
		":type try_rotate: bool\n"
		":param try_rotate: if ``True`` (the default), decoder searches for barcodes in any direction; \n"
		"  if ``False``, it will not search for 90° / 270° rotated barcodes.\n"
		":type try_downscale: bool\n"
		":param try_downscale: if ``True`` (the default), decoder also scans downscaled versions of the input; \n"
		"  if ``False``, it will only search in the resolution provided.\n"
		":type text_mode: zxing.TextMode\n"
		":param text_mode: specifies the TextMode that governs how the raw bytes content is transcoded to text in the Result.\n"
		"  Defaults to :py:attr:`zxing.TextMode.HRI`."
		":type binarizer: zxing.Binarizer\n"
		":param binarizer: the binarizer used to convert image before decoding barcodes.\n"
		"  Defaults to :py:attr:`zxing.Binarizer.LocalAverage`."
		":type is_pure: bool\n"
		":param is_pure: Set to True if the input contains nothing but a perfectly aligned barcode (generated image).\n"
		"  Speeds up detection in that case. Default is False."
		":type ean_add_on_symbol: zxing.EanAddOnSymbol\n"
		":param ean_add_on_symbol: Specify whether to Ignore, Read or Require EAN-2/5 add-on symbols while scanning \n"
		"  EAN/UPC codes. Default is ``Ignore``.\n"
		":rtype: zxing.Result\n"
		":return: a zxing result containing decoded symbol if found, None otherwise"
	);
	m.def("read_barcodes_unwrap_samplegrid", &read_barcodes_unwrap_samplegrid,
		py::arg("image"),
		py::arg("formats") = BarcodeFormats{},
		py::arg("try_rotate") = true,
		py::arg("try_downscale") = true,
		py::arg("text_mode") = TextMode::HRI,
		py::arg("binarizer") = Binarizer::LocalAverage,
		py::arg("is_pure") = false,
		py::arg("ean_add_on_symbol") = EanAddOnSymbol::Ignore,
		"Read (decode) multiple barcodes using Unwarp + SampleGrid strategy from a numpy BGR or grayscale image array or from a PIL image.\n\n"
		"Uses unwarp preprocessing with SampleGridV1 detection strategy for distorted or warped images.\n\n"
		":type image: numpy.ndarray|PIL.Image.Image\n"
		":param image: The image object to decode. The image can be either:\n"
		"  - a numpy array containing image either in grayscale (1 byte per pixel) or BGR mode (3 bytes per pixel)\n"
		"  - a PIL Image\n"
		":type formats: zxing.BarcodeFormat|zxing.BarcodeFormats\n"
		":param formats: the format(s) to decode. If ``None``, decode all formats.\n"
		":type try_rotate: bool\n"
		":param try_rotate: if ``True`` (the default), decoder searches for barcodes in any direction; \n"
		"  if ``False``, it will not search for 90° / 270° rotated barcodes.\n"
		":type try_downscale: bool\n"
		":param try_downscale: if ``True`` (the default), decoder also scans downscaled versions of the input; \n"
		"  if ``False``, it will only search in the resolution provided.\n"
		":type text_mode: zxing.TextMode\n"
		":param text_mode: specifies the TextMode that governs how the raw bytes content is transcoded to text in the Result.\n"
		"  Defaults to :py:attr:`zxing.TextMode.HRI`."
		":type binarizer: zxing.Binarizer\n"
		":param binarizer: the binarizer used to convert image before decoding barcodes.\n"
		"  Defaults to :py:attr:`zxing.Binarizer.LocalAverage`."
		":type is_pure: bool\n"
		":param is_pure: Set to True if the input contains nothing but a perfectly aligned barcode (generated image).\n"
		"  Speeds up detection in that case. Default is False."
		":type ean_add_on_symbol: zxing.EanAddOnSymbol\n"
		":param ean_add_on_symbol: Specify whether to Ignore, Read or Require EAN-2/5 add-on symbols while scanning \n"
		"  EAN/UPC codes. Default is ``Ignore``.\n"
		":rtype: zxing.Result\n"
		":return: a list of zxing results containing decoded symbols, the list is empty if none is found"
		);

	// Unwrap + Standard strategy functions
	m.def("read_barcode_unwrap_standard", &read_barcode_unwrap_standard,
		py::arg("image"),
		py::arg("formats") = BarcodeFormats{},
		py::arg("try_rotate") = true,
		py::arg("try_downscale") = true,
		py::arg("text_mode") = TextMode::HRI,
		py::arg("binarizer") = Binarizer::LocalAverage,
		py::arg("is_pure") = false,
		py::arg("ean_add_on_symbol") = EanAddOnSymbol::Ignore,
		"Read (decode) a barcode using Unwarp + Standard strategy from a numpy BGR or grayscale image array or from a PIL image.\n\n"
		"Uses unwarp preprocessing with standard ZXing detection for distorted or warped images.\n\n"
		":type image: numpy.ndarray|PIL.Image.Image\n"
		":param image: The image object to decode. The image can be either:\n"
		"  - a numpy array containing image either in grayscale (1 byte per pixel) or BGR mode (3 bytes per pixel)\n"
		"  - a PIL Image\n"
		":type formats: zxing.BarcodeFormat|zxing.BarcodeFormats\n"
		":param formats: the format(s) to decode. If ``None``, decode all formats.\n"
		":type try_rotate: bool\n"
		":param try_rotate: if ``True`` (the default), decoder searches for barcodes in any direction; \n"
		"  if ``False``, it will not search for 90° / 270° rotated barcodes.\n"
		":type try_downscale: bool\n"
		":param try_downscale: if ``True`` (the default), decoder also scans downscaled versions of the input; \n"
		"  if ``False``, it will only search in the resolution provided.\n"
		":type text_mode: zxing.TextMode\n"
		":param text_mode: specifies the TextMode that governs how the raw bytes content is transcoded to text in the Result.\n"
		"  Defaults to :py:attr:`zxing.TextMode.HRI`."
		":type binarizer: zxing.Binarizer\n"
		":param binarizer: the binarizer used to convert image before decoding barcodes.\n"
		"  Defaults to :py:attr:`zxing.Binarizer.LocalAverage`."
		":type is_pure: bool\n"
		":param is_pure: Set to True if the input contains nothing but a perfectly aligned barcode (generated image).\n"
		"  Speeds up detection in that case. Default is False."
		":type ean_add_on_symbol: zxing.EanAddOnSymbol\n"
		":param ean_add_on_symbol: Specify whether to Ignore, Read or Require EAN-2/5 add-on symbols while scanning \n"
		"  EAN/UPC codes. Default is ``Ignore``.\n"
		":rtype: zxing.Result\n"
		":return: a zxing result containing decoded symbol if found, None otherwise"
	);
	m.def("read_barcodes_unwrap_standard", &read_barcodes_unwrap_standard,
		py::arg("image"),
		py::arg("formats") = BarcodeFormats{},
		py::arg("try_rotate") = true,
		py::arg("try_downscale") = true,
		py::arg("text_mode") = TextMode::HRI,
		py::arg("binarizer") = Binarizer::LocalAverage,
		py::arg("is_pure") = false,
		py::arg("ean_add_on_symbol") = EanAddOnSymbol::Ignore,
		"Read (decode) multiple barcodes using Unwarp + Standard strategy from a numpy BGR or grayscale image array or from a PIL image.\n\n"
		"Uses unwarp preprocessing with standard ZXing detection for distorted or warped images.\n\n"
		":type image: numpy.ndarray|PIL.Image.Image\n"
		":param image: The image object to decode. The image can be either:\n"
		"  - a numpy array containing image either in grayscale (1 byte per pixel) or BGR mode (3 bytes per pixel)\n"
		"  - a PIL Image\n"
		":type formats: zxing.BarcodeFormat|zxing.BarcodeFormats\n"
		":param formats: the format(s) to decode. If ``None``, decode all formats.\n"
		":type try_rotate: bool\n"
		":param try_rotate: if ``True`` (the default), decoder searches for barcodes in any direction; \n"
		"  if ``False``, it will not search for 90° / 270° rotated barcodes.\n"
		":type try_downscale: bool\n"
		":param try_downscale: if ``True`` (the default), decoder also scans downscaled versions of the input; \n"
		"  if ``False``, it will only search in the resolution provided.\n"
		":type text_mode: zxing.TextMode\n"
		":param text_mode: specifies the TextMode that governs how the raw bytes content is transcoded to text in the Result.\n"
		"  Defaults to :py:attr:`zxing.TextMode.HRI`."
		":type binarizer: zxing.Binarizer\n"
		":param binarizer: the binarizer used to convert image before decoding barcodes.\n"
		"  Defaults to :py:attr:`zxing.Binarizer.LocalAverage`."
		":type is_pure: bool\n"
		":param is_pure: Set to True if the input contains nothing but a perfectly aligned barcode (generated image).\n"
		"  Speeds up detection in that case. Default is False."
		":type ean_add_on_symbol: zxing.EanAddOnSymbol\n"
		":param ean_add_on_symbol: Specify whether to Ignore, Read or Require EAN-2/5 add-on symbols while scanning \n"
		"  EAN/UPC codes. Default is ``Ignore``.\n"
		":rtype: zxing.Result\n"
		":return: a list of zxing results containing decoded symbols, the list is empty if none is found"
		);
#endif

#ifdef ZXING_BUILD_WRITERS
	m.def("write_barcode", &write_barcode,
		py::arg("format"),
		py::arg("text"),
		py::arg("width") = 0,
		py::arg("height") = 0,
		py::arg("quiet_zone") = -1,
		py::arg("ec_level") = -1,
		"Write (encode) a barcode.\n\n"
		":type format: zxing.BarcodeFormat\n"
		":param format: the format to encode\n"
		":type text: str\n"
		":param text: the text to encode\n"
		":type width: int\n"
		":param width: the width in pixels. If ``0``, the width will be the minimum required.\n"
		":type height: int\n"
		":param height: the height in pixels. If ``0``, the height will be the minimum required.\n"
		":type quiet_zone: int\n"
		":param quiet_zone: the size of the quiet zone around the barcode.\n"
		"  If ``-1``, the quiet zone will be the minimum required by the standard.\n"
		":type ec_level: int\n"
		":param ec_level: the error correction level. If ``-1``, the error correction level will be the default for the format.\n"
		":rtype: numpy.ndarray\n"
		":return: a numpy array containing the barcode"
		);
#endif


}
