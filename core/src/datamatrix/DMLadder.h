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
#include "Point.h"
#include "Quadrilateral.h"

#include <array>
#include <cstdint>
#include <utility>

namespace ZXing::DataMatrix::ladder {

/**
 * What the runner itself reads and writes. Every context a plan runs over
 * derives from this; the plan-specific fields come after.
 */
struct State
{
	/// The caller's out-parameter. A step whose detector decodes internally
	/// writes it directly; for every other step the runner decodes into it
	/// once. Valid after a step means: that step won.
	DecoderResult& dec;

	/// Position of the most recent valid detection. What comes back, with
	/// empty bits, when no step wins and the plan falls back to it.
	QuadrilateralI candidate;

	/// The previous step's detection, for a step that re-decodes it rather
	/// than detecting anew, for `when` rules that ask whether anything has
	/// been found yet, and for plans that fall back to it.
	DetectorResult last;
};

/**
 * The context of the entry-point plans (stock, SamplegridV1, DetectDefined).
 * Everything here used to be a local that two or more phases shared; each
 * field says which.
 */
struct Context : State
{
	const BitMatrix& image;
	bool tryHarder;
	bool tryRotate;
	DMGridRefineOptions gridRefine;

	/// Traced by the warp-aware DetectNew step, consumed by the warp-aware
	/// DetectCRPT step that follows it.
	Warp warp;

	/// DetectCRPT's ResultedDefect out-parameter. Set by the first DetectCRPT
	/// step, passed unchanged into the second -- which is why the second one's
	/// result does NOT get it applied (that asymmetry is the original's).
	ResultedDefect crptDefect = ResultedDefect::Default;

	/// The caller-supplied symbol quadrilateral, for plans that have one
	/// (DetectDefined). Unused otherwise.
	std::array<PointF, 4> corners{};
};

/**
 * One phase of a plan over context type Ctx. `run` returns the phase's
 * detection and sets `decodedInside` when its detector has already written
 * ctx.dec for that detection; otherwise the runner decodes a valid detection
 * exactly once. A step applies its own ResultedDefect (or none) before
 * returning, because which defect, and whether one is applied at all, differs
 * per phase and per plan. `when`, if set, gates the step on the context; a
 * step whose rule says no is skipped, not run.
 */
template <class Ctx>
struct Step
{
	const char* name;
	DetectorResult (*run)(Ctx& ctx, bool& decodedInside);
	bool (*when)(const Ctx& ctx) = nullptr;
};

/// What it takes for a step to end the plan.
enum class Policy : uint8_t
{
	/// The step's detection decoded. Steps whose detector decodes for itself
	/// report that through decodedInside; the runner decodes the rest.
	DecodeToWin,
	/// The step produced a valid detection at all. Nothing is decoded here;
	/// that is the caller's, as it is for the stock DataMatrix reader.
	FirstValidDetection,
};

/// What a plan returns when no step wins.
enum class Fallback : uint8_t
{
	/// Empty bits carrying the last valid position -- the ladder's result
	/// candidate, which its reader still reports.
	EmptyWithCandidate,
	/// The last step's detection as it is, valid or not. What DetectCRPT has
	/// always returned: its caller re-decodes or reports it.
	LastDetection,
};

/**
 * Run a plan to its first win. This is the only place that decides what a
 * phase's outcome means, and therefore the only place attribution needs to
 * hook -- calls, time, skips, and which step won are all observable right
 * here.
 *
 * Per step, in order: skip it if its `when` says so; remember the position
 * if the detection is valid; under DecodeToWin, decode it once unless the
 * step already did and return it on success; under FirstValidDetection,
 * return any valid detection as is. A step that did not win leaves its
 * detection in ctx.last for the next one.
 */
template <class Ctx, size_t N>
DetectorResult RunLadder(const Step<Ctx> (&plan)[N], Ctx& ctx, Policy policy = Policy::DecodeToWin,
                         Fallback fallback = Fallback::EmptyWithCandidate)
{
	for (const Step<Ctx>& step : plan) {
		if (step.when && !step.when(ctx))
			continue;

		bool decodedInside = false;
		DetectorResult det = step.run(ctx, decodedInside);

		if (det.isValid()) {
			ctx.candidate = det.position();
			if (policy == Policy::FirstValidDetection)
				return det;
			if (!decodedInside)
				ctx.dec = Decode(det.bits());
		}

		if (policy == Policy::DecodeToWin && ctx.dec.isValid())
			return det;

		ctx.last = std::move(det);
	}

	if (fallback == Fallback::LastDetection)
		return std::move(ctx.last);
	return DetectorResult({}, std::move(ctx.candidate));
}

} // namespace ZXing::DataMatrix::ladder
