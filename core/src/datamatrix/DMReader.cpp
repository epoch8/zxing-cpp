/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
*/
// SPDX-License-Identifier: Apache-2.0

#include "DMReader.h"

#include "BinaryBitmap.h"
#include "DMDecoder.h"
#include "DMDetector.h"
#include "DecodeHints.h"
#include "DecoderResult.h"
#include "DetectorResult.h"
#include "Result.h"

#include <utility>
//#include <android/log.h>
#include <iostream>
//#define LOG_TAG "DMReader"
//#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
//#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)


#define LOG_TAG "DMReader"
#define LOGI(...) std::cout << LOG_TAG << ": " << __VA_ARGS__ << std::endl
#define LOGE(...) std::cerr << LOG_TAG << ": " << __VA_ARGS__ << std::endl


// mute logs
//#undef LOGI
//#undef LOGE
//#define LOGI(...) (void)0
//#define LOGE(...) (void)0


namespace ZXing::DataMatrix {

    Result Reader::decode(const BinaryBitmap& image) const
    {
        LOGI("DataMatrix::Reader::decode - Starting to decode DataMatrix.");
#ifdef __cpp_impl_coroutine
        LOGI("DataMatrix::Reader::decode - Choosing coroutines.");
        return FirstOrDefault(decode(image, 1));
#else
        LOGI("DataMatrix::Reader::decode - Finished coroutines code.");
        auto binImg = image.getBitMatrix();
        LOGI("DataMatrix::Reader::decode - Exited getBitMatrix.");
        if (binImg == nullptr) {
            LOGI("DataMatrix::Reader::decode - No bit matrix found in the image.");
            return {};
        }
        LOGI("DataMatrix::Reader::decode - Going to detectorResult");
        auto detectorResult = Detect(*binImg, _hints.tryHarder(), _hints.tryRotate(), _hints.isPure());
        if (!detectorResult.isValid()) {
            LOGI("DataMatrix::Reader::decode - Detector result is invalid.");
            return {};
        }

        auto decodeResult = Decode(detectorResult.bits());
        LOGI("DataMatrix::Reader::decode - Decoding successful, returning result.");
        return Result(std::move(decodeResult), std::move(detectorResult).position(), BarcodeFormat::DataMatrix);
#endif
    }

#ifdef __cpp_impl_coroutine
    Results Reader::decode(const BinaryBitmap& image, int maxSymbols) const
{
    LOGI("DataMatrix::Reader::decode - Starting to decode multiple DataMatrix codes.");
    auto binImg = image.getBitMatrix();
    if (binImg == nullptr) {
        LOGI("DataMatrix::Reader::decode - No bit matrix found in the image.");
        return {};
    }

    Results results;
    for (auto&& detRes : Detect(*binImg, _hints.tryHarder(), _hints.tryRotate(), _hints.isPure())) {
        auto decRes = Decode(detRes.bits());
        if (decRes.isValid(_hints.returnErrors())) {
            results.emplace_back(std::move(decRes), std::move(detRes).position(), BarcodeFormat::DataMatrix);
            if (maxSymbols > 0 && Size(results) >= maxSymbols) {
                LOGI("DataMatrix::Reader::decode - Maximum number of symbols reached.");
                break;
            }
        }
    }

    LOGI("DataMatrix::Reader::decode - Decoding multiple DataMatrix codes completed.");
    return results;
}
#endif
} // namespace ZXing::DataMatrix
