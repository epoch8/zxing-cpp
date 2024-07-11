/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
*/
// SPDX-License-Identifier: Apache-2.0

#include "MultiFormatReader.h"

#include "BarcodeFormat.h"
#include "BinaryBitmap.h"
#include "DecodeHints.h"
#include "aztec/AZReader.h"
#include "datamatrix/DMReader.h"
#include "maxicode/MCReader.h"
#include "oned/ODReader.h"
#include "pdf417/PDFReader.h"
#include "qrcode/QRReader.h"

#include <memory>
#include <iostream>

// Helper function to print log messages.
template <typename... Args>
void LogInfo(const char* tag, Args... args) {
    (std::cout << ... << args) << std::endl;
}

// Helper function to print error messages.
template <typename... Args>
void LogError(const char* tag, Args... args) {
    (std::cerr << ... << args) << std::endl;
}

//#include <android/log.h>

//#define LOG_TAG "FormatReader"
//#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
//#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define LOG_TAG "FormatReader"
#define LOGI(...) LogInfo(LOG_TAG, __VA_ARGS__)
#define LOGE(...) LogError(LOG_TAG, __VA_ARGS__)


// mute logs
//#undef LOGI
//#undef LOGE
//#define LOGI(...) (void)0
//#define LOGE(...) (void)0

namespace ZXing {

MultiFormatReader::MultiFormatReader(const DecodeHints& hints) : _hints(hints)
{
	auto formats = hints.formats().empty() ? BarcodeFormat::Any : hints.formats();

	// Put linear readers upfront in "normal" mode
	if (formats.testFlags(BarcodeFormat::LinearCodes) && !hints.tryHarder())
		_readers.emplace_back(new OneD::Reader(hints));

	if (formats.testFlags(BarcodeFormat::QRCode | BarcodeFormat::MicroQRCode))
		_readers.emplace_back(new QRCode::Reader(hints, true));
	if (formats.testFlag(BarcodeFormat::DataMatrix))
		_readers.emplace_back(new DataMatrix::Reader(hints, true));
	if (formats.testFlag(BarcodeFormat::Aztec))
		_readers.emplace_back(new Aztec::Reader(hints, true));
	if (formats.testFlag(BarcodeFormat::PDF417))
		_readers.emplace_back(new Pdf417::Reader(hints));
	if (formats.testFlag(BarcodeFormat::MaxiCode))
		_readers.emplace_back(new MaxiCode::Reader(hints));

	// At end in "try harder" mode
	if (formats.testFlags(BarcodeFormat::LinearCodes) && hints.tryHarder())
		_readers.emplace_back(new OneD::Reader(hints));
}

MultiFormatReader::~MultiFormatReader() = default;

Result MultiFormatReader::read(const BinaryBitmap& image) const {
    LOGI("MultiFormatReader::read - Starting to decode single barcode.");
    Result r;
    for (const auto& reader : _readers) {
        LOGI("Attempting to decode with a barcode reader.");
        r = reader->decode(image);
        if (r.isValid()) {
            LOGI("Barcode decoding succeeded with a reader.");
            return r;
        }
    }
    if (_hints.returnErrors()) {
        LOGI("Returning last decoding result as per hints configuration.");
        return r;
    } else {
        LOGI("No valid barcode found, returning empty result.");
        return Result();
    }
}

Results MultiFormatReader::readMultiple(const BinaryBitmap& image, int maxSymbols) const {
    LOGI("MultiFormatReader::readMultiple - Starting to decode multiple barcodes.");
    std::vector<Result> res;

    for (const auto& reader : _readers) {
        if (image.inverted() && !reader->supportsInversion) {
            LOGI("Skipping reader that does not support inversion for an inverted image.");
            continue;
        }
        LOGI("Attempting to decode with a barcode reader.");
        auto r = reader->decode(image, maxSymbols);
        if (!_hints.returnErrors()) {
            LOGI("Removing invalid results from the list.");
            //TODO: C++20 res.erase_if()
            auto it = std::remove_if(res.begin(), res.end(), [](auto&& r) { return !r.isValid(); });
            res.erase(it, res.end());
        }
        maxSymbols -= Size(r);
        LOGI("Decoded %d barcodes with the current reader.", Size(r));
        res.insert(res.end(), std::move_iterator(r.begin()), std::move_iterator(r.end()));
        if (maxSymbols <= 0) {
            LOGI("Maximum number of symbols reached, breaking out of the loop.");
            break;
        }
    }

    LOGI("Sorting the decoded barcodes based on their position.");
    // sort results based on their position on the image
    std::sort(res.begin(), res.end(), [](const Result& l, const Result& r) {
        auto lp = l.position().topLeft();
        auto rp = r.position().topLeft();
        return lp.y < rp.y || (lp.y == rp.y && lp.x < rp.x);
    });

    LOGI("MultiFormatReader::readMultiple - Decoding multiple barcodes completed.");
    return res;
}

} // ZXing
