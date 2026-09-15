/*
* Copyright 2026 CRPT
*/
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BitMatrix.h"
#include "DecodeHints.h"
#include "DecoderResult.h"
#include "DetectorResult.h"
#include "DMDecoder.h"
#include "GridSampler.h"
#include "Quadrilateral.h"

#include <cstdint>
#include <utility>

namespace ZXing::DataMatrix::ladder {

/**
 * What a detector plan's steps read and write. Everything here used to be a
 * local of DetectSamplegridV1 that two or more phases shared; each field says
 * which.
 */
struct Context
{
	const BitMatrix& image;
	bool tryHarder;
	bool tryRotate;
	DMGridRefineOptions gridRefine;

	/// The caller's out-parameter. A step whose detector decodes internally
	/// (DetectOldWithOffsets, DetectCRPT) writes it directly; for every other
	/// step the runner decodes into it once. Valid after a step means: return.
	DecoderResult& dec;

	/// Traced by the warp-aware DetectNew step, consumed by the warp-aware
	/// DetectCRPT step that follows it.
	Warp warp;

	/// DetectCRPT's ResultedDefect out-parameter. Set by the first DetectCRPT
	/// step, passed unchanged into the second -- which is why the second one's
	/// result does NOT get it applied (that asymmetry is the original's).
	ResultedDefect crptDefect = ResultedDefect::Default;

	/// Position of the most recent valid detection. What comes back, with
	/// empty bits, when no step decodes.
	QuadrilateralI candidate;

	/// The previous step's detection, for a step that re-decodes it rather
	/// than detecting anew (the PrintShift re-decode after the warp DetectCRPT).
	DetectorResult last;
};

/**
 * One phase of a plan. `run` returns the phase's detection. It sets
 * `decodedInside` when its detector has already written ctx.dec for that
 * detection; otherwise the runner decodes a valid detection exactly once.
 * The step applies its own ResultedDefect to the detection before returning,
 * because which defect, and whether it is applied at all, differs per phase.
 */
struct Step
{
	const char* name;
	DetectorResult (*run)(Context& ctx, bool& decodedInside);
};

/**
 * Run a plan to its first decode. This is the only place that decides what a
 * phase's outcome means, and therefore the only place attribution needs to
 * hook -- calls, time, and which step won are all observable right here.
 *
 * Per step, in order: remember the position if the detection is valid; decode
 * it once unless the step already did; return it if that decode succeeded;
 * otherwise keep it for the next step and move on. When nothing decodes, the
 * result carries empty bits and the last valid position.
 */
template <size_t N>
DetectorResult RunLadder(const Step (&plan)[N], Context& ctx)
{
	for (const Step& step : plan) {
		bool decodedInside = false;
		DetectorResult det = step.run(ctx, decodedInside);

		if (det.isValid())
			ctx.candidate = det.position();

		if (!decodedInside && det.isValid())
			ctx.dec = Decode(det.bits());

		if (ctx.dec.isValid())
			return det;

		ctx.last = std::move(det);
	}
	return DetectorResult({}, std::move(ctx.candidate));
}

} // namespace ZXing::DataMatrix::ladder
