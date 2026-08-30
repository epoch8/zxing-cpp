/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
*/
// SPDX-License-Identifier: Apache-2.0

#include "DMReader.h"
#include "CrptProfile.h"

#include "BinaryBitmap.h"
#include "DMDecoder.h"
#include "DMDetector.h"
#include "DecodeHints.h"
#include "DecoderResult.h"
#include "DetectorResult.h"
#include "Result.h"

#include <utility>

namespace ZXing::DataMatrix {

Result Reader::decode(const BinaryBitmap& image) const
{
#ifdef __cpp_impl_coroutine
	return FirstOrDefault(decode(image, 1));
#else
	// Same lazy binarization as DMCRPTReader::decode -- getBitMatrix() is where
	// HybridBinarizer actually runs. This path (ZXingStandard) was untimed, so
	// binarizer_ns previously reflected only the samplegrid reader.
	const BitMatrix* binImg;
	{
		CRPT_ZX_SCOPED_NS(binarizer_ns, binarizer_calls);
		binImg = image.getBitMatrix();
	}
	if (binImg == nullptr)
		return {};

	auto detectorResult = Detect(*binImg, _hints.tryHarder(), _hints.tryRotate(), _hints.isPure(), _hints.dmGridRefine());
	if (!detectorResult.isValid())
		return {};

	// C7 on the reader side: decode_res_ns only covers DecodeResult() calls made
	// from inside DMDetector, so this Reed-Solomon pass was invisible.
	DecoderResult decoderResult;
	{
		CRPT_ZX_SCOPED_NS(dm_decode_ns, dm_decode_calls);
		decoderResult = Decode(detectorResult.bits());
	}
	Result res = Result(std::move(decoderResult), std::move(detectorResult).position(), BarcodeFormat::DataMatrix);
	res.setResultedDefect(detectorResult.resultedDefect());
	return res;
#endif
}

Result Reader::decode(const BinaryBitmap& image, const PointF& P0, const PointF& P1, const PointF& P2, const PointF& P3)
{
	const BitMatrix* binImg;
	{
		CRPT_ZX_SCOPED_NS(binarizer_ns, binarizer_calls);
		binImg = image.getBitMatrix();
	}
	if (binImg == nullptr)
		return {};

	DecoderResult decoderResult;
	auto detectorResult = DetectDefined(*binImg, P0, P1, P2, P3, _hints.tryHarder(), _hints.tryRotate(), _hints.isPure(), decoderResult,
										_hints.dmGridRefine());

	if (!detectorResult.isValid()) return {};

	return Result(std::move(decoderResult), std::move(detectorResult).position(), BarcodeFormat::DataMatrix);
}

#ifdef __cpp_impl_coroutine
Results Reader::decode(const BinaryBitmap& image, int maxSymbols) const
{
	auto binImg = image.getBitMatrix();
	if (binImg == nullptr)
		return {};

	Results results;
	for (auto&& detRes : Detect(*binImg, _hints.tryHarder(), _hints.tryRotate(), _hints.isPure(), _hints.dmGridRefine())) {
		auto decRes = Decode(detRes.bits());
		if (decRes.isValid(_hints.returnErrors())) {
			results.emplace_back(std::move(decRes), std::move(detRes).position(), BarcodeFormat::DataMatrix);
			if (maxSymbols > 0 && Size(results) >= maxSymbols)
				break;
		}
	}

	return results;
}
#endif


Result DMCRPTReader::decode(const BinaryBitmap& image) const
{
	// The real binarization cost: getBitMatrix() is where HybridBinarizer
	// actually runs (CreateBitmap merely constructs it).
	const BitMatrix* binImg;
	{
		CRPT_ZX_SCOPED_NS(binarizer_ns, binarizer_calls);
		binImg = image.getBitMatrix();
	}
	if (binImg == nullptr)
		return {};

	DecoderResult decoderResult;
	auto detectorResult = DetectSamplegridV1(*binImg, _hints.tryHarder(), _hints.tryRotate(), _hints.isPure(), decoderResult,
											 _hints.dmGridRefine());

	if (!decoderResult.isValid() && detectorResult.isValid()) {
		CRPT_ZX_SCOPED_NS(dm_decode_ns, dm_decode_calls);
		decoderResult = Decode(detectorResult.bits());
	}

	Result res = Result(std::move(decoderResult), std::move(detectorResult).position(), BarcodeFormat::DataMatrix);
	res.setResultedDefect(detectorResult.resultedDefect());
	return res;
}
#ifdef __cpp_impl_coroutine
Result DMCRPTReader::decode(const BinaryBitmap& image, int maxSymbols) const
{
	auto binImg = image.getBitMatrix();
	if (binImg == nullptr)
		return {};

	DecoderResult decoderResult;
	auto detectorResult = DetectSamplegridV1(*binImg, _hints.tryHarder(), _hints.tryRotate(), _hints.isPure(), decoderResult,
											 _hints.dmGridRefine());

	if (!detectorResult.isValid()) return {};

	return Result(std::move(decoderResult), std::move(detectorResult).position(), BarcodeFormat::DataMatrix);
}
#endif
} // namespace ZXing::DataMatrix
