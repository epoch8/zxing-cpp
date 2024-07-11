/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
* Copyright 2022 Axel Waggershauser
*/
// SPDX-License-Identifier: Apache-2.0

#include "AZReader.h"

#include "AZDecoder.h"
#include "AZDetector.h"
#include "AZDetectorResult.h"
#include "BinaryBitmap.h"
#include "DecodeHints.h"
#include "DecoderResult.h"
#include "Result.h"

#include <memory>
#include <utility>
#include <iostream>
//#include <android/log.h>

//#define LOG_TAG "AZreader"
//#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
//#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define LOG_TAG "AZreader"
#define LOGI(...) std::cout << LOG_TAG << ": " << __VA_ARGS__ << std::endl
#define LOGE(...) std::cerr << LOG_TAG << ": " << __VA_ARGS__ << std::endl

// mute logs
#undef LOGI
#undef LOGE
#define LOGI(...) (void)0
#define LOGE(...) (void)0

namespace ZXing::Aztec {

    Result Reader::decode(const BinaryBitmap& image) const {
        LOGI("Aztec::Reader::decode - Attempting to decode a single Aztec code.");
        auto binImg = image.getBitMatrix();
        if (binImg == nullptr) {
            LOGI("Aztec::Reader::decode - No binary image available, returning empty result.");
            return {};
        }

        DetectorResult detectorResult = Detect(*binImg, _hints.isPure(), _hints.tryHarder());
        if (!detectorResult.isValid()) {
            LOGI("Aztec::Reader::decode - Detection failed, returning empty result.");
            return {};
        }

        auto decodeResult = Decode(detectorResult)
                .setReaderInit(detectorResult.readerInit())
                .setIsMirrored(detectorResult.isMirrored())
                .setVersionNumber(detectorResult.nbLayers());

        LOGI("Aztec::Reader::decode - Decoding successful, returning result.");
        return Result(std::move(decodeResult), std::move(detectorResult).position(), BarcodeFormat::Aztec);
    }

    Results Reader::decode(const BinaryBitmap& image, int maxSymbols) const {
        LOGI("Aztec::Reader::decode - Attempting to decode multiple Aztec codes.");
        auto binImg = image.getBitMatrix();
        if (binImg == nullptr) {
            LOGI("Aztec::Reader::decode - No binary image available, returning empty results.");
            return {};
        }

        auto detRess = Detect(*binImg, _hints.isPure(), _hints.tryHarder(), maxSymbols);

        Results results;
        for (auto&& detRes : detRess) {
            auto decRes =
                    Decode(detRes).setReaderInit(detRes.readerInit()).setIsMirrored(detRes.isMirrored()).setVersionNumber(detRes.nbLayers());
            if (decRes.isValid(_hints.returnErrors())) {
                results.emplace_back(std::move(decRes), std::move(detRes).position(), BarcodeFormat::Aztec);
                LOGI("Aztec::Reader::decode - Aztec code decoded and added to results.");
                if (maxSymbols > 0 && Size(results) >= maxSymbols) {
                    LOGI("Aztec::Reader::decode - Maximum number of symbols reached, stopping decoding.");
                    break;
                }
            } else {
                LOGI("Aztec::Reader::decode - Decoded result is not valid, skipping.");
            }
        }

        LOGI("Aztec::Reader::decode - Finished decoding multiple Aztec codes.");
        return results;
    }

} // namespace ZXing::Aztec // namespace ZXing::Aztec
