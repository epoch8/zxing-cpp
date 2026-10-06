/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
*/
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Point.h"

#include <vector>

namespace ZXing {

class DecoderResult;
class BitMatrix;

namespace DataMatrix {

DecoderResult Decode(const BitMatrix& bits);

/** Soft decode metric for grid refinement: lower score is better. */
struct DecodeScore {
	bool ok = false;
	int failedBlocks = 0;
	int errorsCorrected = 0;
	int totalBlocks = 0;
	/** Full-symbol module coords of bits belonging to RS-corrected codewords (may be empty if RS failed). */
	std::vector<PointI> errorModules;

	/** Lexicographic score: prefer ok, then fewer failed blocks, then fewer corrected errors. */
	int score() const
	{
		if (ok)
			return errorsCorrected;
		return 1000000 + failedBlocks * 1000 + errorsCorrected;
	}
};

DecodeScore EvaluateDecode(const BitMatrix& bits);

} // DataMatrix
} // ZXing
