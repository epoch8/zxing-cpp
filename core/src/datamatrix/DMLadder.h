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
#include "DMLadderSpec.h"
#include "GridSampler.h"
#include "Point.h"
#include "Quadrilateral.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace ZXing::DataMatrix::ladder {

/// The feature set in force for the decode in progress, set by the entry
/// point before it builds its context. Thread-local because decodes run on
/// worker threads; read once per context construction, never on a hot path.
inline Features& CurrentFeatures() noexcept
{
	thread_local Features f;
	return f;
}

/// The two DMGridRefineOptions flags as features, over the registry defaults.
inline Features FeaturesFrom(DMGridRefineOptions g) noexcept
{
	return Features::Defaults().set(Feat::regionGrowing, g.regionGrowing).set(Feat::rsFeedback, g.rsFeedback);
}

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

	/// The effective feature set. Every context, sub-ladders included, picks
	/// it up from the thread-local the entry point set, so a gate anywhere
	/// below is `if (ctx.on(Feat::x))` and nothing is threaded to reach it.
	Features features = CurrentFeatures();

	bool on(Feat f) const noexcept { return features.on(f); }
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

/// A plan names at most this many steps; the resolved sequence lives on the stack.
constexpr size_t kMaxPlanSteps = 16;

/**
 * Run a sequence of steps to its first win. This is the only place that
 * decides what a phase's outcome means, and therefore the only place
 * attribution needs to hook -- calls, time, skips, and which step won are all
 * observable right here.
 *
 * Per step, in order: skip it if its `when` says so; remember the position
 * if the detection is valid; under DecodeToWin, decode it once unless the
 * step already did and return it on success; under FirstValidDetection,
 * return any valid detection as is. A step that did not win leaves its
 * detection in ctx.last for the next one.
 */
template <class Ctx>
DetectorResult RunSteps(const Step<Ctx>* const* first, const Step<Ctx>* const* last, Ctx& ctx, Policy policy, Fallback fallback)
{
	for (; first != last; ++first) {
		const Step<Ctx>& step = **first;
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

/// Run a plan in its built-in order.
template <class Ctx, size_t N>
DetectorResult RunLadder(const Step<Ctx> (&plan)[N], Ctx& ctx, Policy policy = Policy::DecodeToWin,
                         Fallback fallback = Fallback::EmptyWithCandidate)
{
	static_assert(N <= kMaxPlanSteps, "plan longer than kMaxPlanSteps");
	const Step<Ctx>* seq[N];
	for (size_t i = 0; i < N; ++i)
		seq[i] = &plan[i];
	return RunSteps(seq, seq + N, ctx, policy, fallback);
}

/**
 * Run a plan in the order a Spec names, or its built-in order when the Spec
 * names no steps. A name the plan does not have is a hard error: a typo in a
 * schedule fails the run rather than silently running something else.
 */
template <class Ctx, size_t N>
DetectorResult RunLadder(const Step<Ctx> (&plan)[N], const Spec& spec, Ctx& ctx, Policy policy = Policy::DecodeToWin,
                         Fallback fallback = Fallback::EmptyWithCandidate)
{
	if (spec.steps.empty())
		return RunLadder(plan, ctx, policy, fallback);
	if (spec.steps.size() > kMaxPlanSteps)
		throw std::invalid_argument("ladder spec names more than " + std::to_string(kMaxPlanSteps) + " steps");

	const Step<Ctx>* seq[kMaxPlanSteps];
	size_t n = 0;
	for (const std::string& name : spec.steps) {
		const Step<Ctx>* found = nullptr;
		for (const Step<Ctx>& step : plan)
			if (name == step.name) {
				found = &step;
				break;
			}
		if (!found)
			throw std::invalid_argument("unknown ladder step: " + name);
		seq[n++] = found;
	}
	return RunSteps(seq, seq + n, ctx, policy, fallback);
}

} // namespace ZXing::DataMatrix::ladder
