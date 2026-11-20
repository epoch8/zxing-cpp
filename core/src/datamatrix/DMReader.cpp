/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
*/
// SPDX-License-Identifier: Apache-2.0

#include "DMReader.h"

#include "BinaryBitmap.h"
#include "DMDetector.h"
#include "DecodeHints.h"
#include "DecoderResult.h"
#include "DetectorResult.h"
#include "Result.h"

#include <utility>

namespace ZXing::DataMatrix {

Result Reader::decode(const BinaryBitmap& image) const
{
	auto binImg = image.getBitMatrix();
	if (binImg == nullptr)
		return {};

	// The stock plan decodes the detection it ends on (FirstValidDetection), so
	// the reader only wraps what comes back.
	DecoderResult decoderResult;
	auto detectorResult = Detect(*binImg, _hints.tryHarder(), _hints.tryRotate(), _hints.isPure(), decoderResult, _hints.dmGridRefine(),
								 ladder::ParseSpec(_hints.dmLadder()));
	if (!detectorResult.isValid())
		return {};
	Result res = Result(std::move(decoderResult), std::move(detectorResult).position(), BarcodeFormat::DataMatrix);
	res.setResultedDefect(detectorResult.resultedDefect());
	return res;
}

Result Reader::decode(const BinaryBitmap& image, const PointF& P0, const PointF& P1, const PointF& P2, const PointF& P3)
{
	auto binImg = image.getBitMatrix();
	if (binImg == nullptr)
		return {};

	DecoderResult decoderResult;
	auto detectorResult = DetectDefined(*binImg, P0, P1, P2, P3, _hints.tryHarder(), _hints.tryRotate(), _hints.isPure(), decoderResult,
										_hints.dmGridRefine(), ladder::ParseSpec(_hints.dmLadder()));

	if (!detectorResult.isValid()) return {};

	return Result(std::move(decoderResult), std::move(detectorResult).position(), BarcodeFormat::DataMatrix);
}



Result DMCRPTReader::decode(const BinaryBitmap& image) const
{
	auto binImg = image.getBitMatrix();
	if (binImg == nullptr)
		return {};

	DecoderResult decoderResult;
	auto detectorResult = DetectSamplegridV1(*binImg, _hints.tryHarder(), _hints.tryRotate(), _hints.isPure(), decoderResult,
											 _hints.dmGridRefine(), ladder::ParseSpec(_hints.dmLadder()));

	// No re-decode: the plan decodes what it returns valid (DecodeToWin), and
	// when nothing wins it returns empty bits (EmptyWithCandidate), so a valid
	// detection with an undecoded result cannot come back from it.

	Result res = Result(std::move(decoderResult), std::move(detectorResult).position(), BarcodeFormat::DataMatrix);
	res.setResultedDefect(detectorResult.resultedDefect());
	return res;
}
} // namespace ZXing::DataMatrix
