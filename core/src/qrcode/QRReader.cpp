/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
* Copyright 2022 Axel Waggershauser
*/
// SPDX-License-Identifier: Apache-2.0

#include "QRReader.h"

#include "BinaryBitmap.h"
#include "ConcentricFinder.h"
#include "DecodeHints.h"
#include "DecoderResult.h"
#include "DetectorResult.h"
#include "LogMatrix.h"
#include "QRDecoder.h"
#include "QRDetector.h"
#include "Result.h"

#include <utility>
#include <iostream>
//#include <android/log.h>

//#define LOG_TAG "QRreader"
//#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
//#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)


#define LOG_TAG "QRreader"
#define LOGI(...) std::cout << LOG_TAG << ": " << __VA_ARGS__ << std::endl
#define LOGE(...) std::cerr << LOG_TAG << ": " << __VA_ARGS__ << std::endl

// mute logs
//#undef LOGI
//#undef LOGE
//#define LOGI(...) (void)0
//#define LOGE(...) (void)0


namespace ZXing::QRCode {

    Result Reader::decode(const BinaryBitmap& image) const
    {
        LOGI("QRCode::Reader::decode - Starting to decode QR Code.");
#if 1
        if (!_hints.isPure()) {
            LOGI("QRCode::Reader::decode - Decoding using FirstOrDefault for non-pure mode.");
            return FirstOrDefault(decode(image, 1));
        }
#endif

        auto binImg = image.getBitMatrix();
        if (binImg == nullptr) {
            LOGI("QRCode::Reader::decode - No bit matrix found in the image.");
            return {};
        }

        DetectorResult detectorResult;
        if (_hints.hasFormat(BarcodeFormat::QRCode))
            detectorResult = DetectPureQR(*binImg);
        if (_hints.hasFormat(BarcodeFormat::MicroQRCode) && !detectorResult.isValid())
            detectorResult = DetectPureMQR(*binImg);

        if (!detectorResult.isValid()) {
            LOGI("QRCode::Reader::decode - Detector result is invalid.");
            return {};
        }

        auto decoderResult = Decode(detectorResult.bits());
        auto position = detectorResult.position();

        LOGI("QRCode::Reader::decode - Decoding successful, returning result.");
        return Result(std::move(decoderResult), std::move(position),
                      detectorResult.bits().width() < 21 ? BarcodeFormat::MicroQRCode : BarcodeFormat::QRCode);
    }

    void logFPSet(const FinderPatternSet& fps [[maybe_unused]])
    {
        LOGI("QRCode::Reader::logFPSet - Logging FinderPatternSet.");
#ifdef PRINT_DEBUG
        auto drawLine = [](PointF a, PointF b) {
        int steps = maxAbsComponent(b - a);
        PointF dir = bresenhamDirection(PointF(b - a));
        for (int i = 0; i < steps; ++i)
            log(centered(a + i * dir), 2);
    };

    drawLine(fps.bl, fps.tl);
    drawLine(fps.tl, fps.tr);
    drawLine(fps.tr, fps.bl);
#endif
    }

    Results Reader::decode(const BinaryBitmap& image, int maxSymbols) const
    {
        LOGI("QRCode::Reader::decode - Starting to decode multiple QR Codes.");
        auto binImg = image.getBitMatrix();
        if (binImg == nullptr) {
            LOGI("QRCode::Reader::decode - No bit matrix found in the image.");
            return {};
        }

#ifdef PRINT_DEBUG
        LogMatrixWriter lmw(log, *binImg, 5, "qr-log.pnm");
#endif

        auto allFPs = FindFinderPatterns(*binImg, _hints.tryHarder());

#ifdef PRINT_DEBUG
        printf("allFPs: %d\n", Size(allFPs));
#endif

        std::vector<ConcentricPattern> usedFPs;
        Results results;

        if (_hints.hasFormat(BarcodeFormat::QRCode)) {
            auto allFPSets = GenerateFinderPatternSets(allFPs);
            for (const auto& fpSet : allFPSets) {
                if (Contains(usedFPs, fpSet.bl) || Contains(usedFPs, fpSet.tl) || Contains(usedFPs, fpSet.tr))
                    continue;

                logFPSet(fpSet);

                auto detectorResult = SampleQR(*binImg, fpSet);
                if (detectorResult.isValid()) {
                    auto decoderResult = Decode(detectorResult.bits());
                    auto position = detectorResult.position();
                    if (decoderResult.isValid()) {
                        usedFPs.push_back(fpSet.bl);
                        usedFPs.push_back(fpSet.tl);
                        usedFPs.push_back(fpSet.tr);
                    }
                    if (decoderResult.isValid(_hints.returnErrors())) {
                        results.emplace_back(std::move(decoderResult), std::move(position), BarcodeFormat::QRCode);
                        if (maxSymbols && Size(results) == maxSymbols) {
                            LOGI("QRCode::Reader::decode - Maximum number of symbols reached.");
                            break;
                        }
                    }
                }
            }
        }

        if (_hints.hasFormat(BarcodeFormat::MicroQRCode) && !(maxSymbols && Size(results) == maxSymbols)) {
            for (const auto& fp : allFPs) {
                if (Contains(usedFPs, fp))
                    continue;

                auto detectorResult = SampleMQR(*binImg, fp);
                if (detectorResult.isValid()) {
                    auto decoderResult = Decode(detectorResult.bits());
                    auto position = detectorResult.position();
                    if (decoderResult.isValid(_hints.returnErrors())) {
                        results.emplace_back(std::move(decoderResult), std::move(position), BarcodeFormat::MicroQRCode);
                        if (maxSymbols && Size(results) == maxSymbols)
                            break;
                    }

                }
            }
        }

        return results;
    }

} // namespace ZXing::QRCode
