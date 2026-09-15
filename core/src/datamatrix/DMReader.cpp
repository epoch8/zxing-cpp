/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
*/
// SPDX-License-Identifier: Apache-2.0

#include "DMReader.h"
#include "../CrptTrace.h"
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

	// Timed from BEFORE Detect() so the fail figure includes a detection that
	// found nothing -- the common case, and the one that decides whether stock
	// can be moved earlier in the ladder.
	CRPT_ZX_T0(_t_stock);
	auto detectorResult = Detect(*binImg, _hints.tryHarder(), _hints.tryRotate(), _hints.isPure(), _hints.dmGridRefine());
	if (!detectorResult.isValid()) {
		CRPT_ZX_T1(_t_stock, stock_fail_ns, stock_fail_calls);
		return {};
	}

	// C7 on the reader side: decode_res_ns only covers DecodeResult() calls made
	// from inside DMDetector, so this Reed-Solomon pass was invisible.
	DecoderResult decoderResult;
	{
		CRPT_ZX_SCOPED_NS(dm_decode_ns, dm_decode_calls);
		decoderResult = Decode(detectorResult.bits());
	}
	if (detectorResult.isValid())
		CRPT_REGION_PAY("crptreader", *binImg, detectorResult.position(),
		                (long)(decoderResult.isValid() ? decoderResult.content().bytes.size() : 0));
	if (decoderResult.isValid())
		CRPT_ZX_T1(_t_stock, stock_win_ns, stock_win);
	else
		CRPT_ZX_T1(_t_stock, stock_fail_ns, stock_fail_calls);
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

	if (decoderResult.isValid()) CRPT_ZX_COUNT(defined_win);
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
		CRPT_REGION_PAY("stockreader", *binImg, detRes.position(),
		                (long)(decRes.isValid() ? decRes.content().bytes.size() : 0));
		CRPT_T("reader_decode", decRes.isValid(_hints.returnErrors()) ? 1 : 0,
		       detRes.bits().width());
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
		{
			CRPT_ZX_SCOPED_NS(dm_decode_ns, dm_decode_calls);
			decoderResult = Decode(detectorResult.bits());
		}
		// No sgv1_win_s* fired for this one: DetectSamplegridV1 handed back a
		// detection whose decode it had already failed, and this second attempt
		// succeeded. Counted separately so the win set stays exhaustive.
		if (decoderResult.isValid()) CRPT_ZX_COUNT(sgv1_tail_win);
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
