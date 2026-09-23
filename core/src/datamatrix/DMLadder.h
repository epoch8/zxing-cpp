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
#include "DMLadderStats.h"
#include "GridSampler.h"
#include "PerspectiveTransform.h"
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

/// The Decoder stage: the module bits a sampler produced -> content. Swappable
/// per decode, like the feature set: the entry point installs one (the
/// standard DataMatrix decoder unless a recipe names another), every context
/// picks it up, and every decode under the runner -- the runner's own and the
/// ones a variant loop makes -- goes through it. Swap it here and it is
/// swapped everywhere below.
using DecodeFn = DecoderResult (*)(const BitMatrix& bits);

inline DecodeFn& CurrentDecoder() noexcept
{
	thread_local DecodeFn d = &Decode;
	return d;
}

/// Where a symbol's modules lie in the image: its size in modules, the
/// module -> pixel transform, and the per-row/column warp if one was traced.
/// What a locator or refiner hands the Sampling stage.
struct Geometry
{
	int width = 0;
	int height = 0;
	PerspectiveTransform mod2pix;
	const Warp* warp = nullptr;
};

/// A sample, with the geometry it was actually taken at: a sampler may move
/// the corners (the offset-testing one does), and whatever refines the sample
/// has to work from where it was taken, not from where it was asked for.
struct ModuleGrid
{
	DetectorResult det;
	Geometry geometry;
};

using SampleFn = ModuleGrid (*)(const BitMatrix& image, const Geometry& g);
using RefineFn = DetectorResult (*)(const BitMatrix& image, const Geometry& g, DMGridRefineOptions refine, DetectorResult sample);

/// The Sampling stage: a named sampler, and the refinement of what it sampled
/// (none if null). The refinement is where region growing and Reed-Solomon
/// feedback live, gated by the features; with both off it returns the sample.
/// Each step names the Sampling it uses (DMDetector.cpp, kSampling*), and the
/// runner decodes the result -- so a step is: geometry -> Sampling -> Decoder.
struct Sampling
{
	const char* name;
	SampleFn sample;
	RefineFn refine = nullptr;

	DetectorResult operator()(const BitMatrix& image, const Geometry& g, DMGridRefineOptions gridRefine) const
	{
		ModuleGrid m = sample(image, g);
		return refine ? refine(image, m.geometry, gridRefine, std::move(m.det)) : std::move(m.det);
	}
};

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
	/// The caller's out-parameter, the decode of the detection the plan
	/// returns. The runner writes it; a step hosting a nested plan lets that
	/// plan's runner write it (decodedInside). Valid after a step means: that
	/// step won.
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

	/// The Decoder stage in force (CurrentDecoder). Nothing under the runner
	/// calls Decode directly.
	DecodeFn decoder = CurrentDecoder();

	DecoderResult decode(const BitMatrix& bits) const { return decoder(bits); }
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
 * detection; the runner decodes a valid one exactly once, through ctx.decode.
 * A step that hosts a nested plan sets `decodedInside`: that plan's runner has
 * already decoded the detection it returns into ctx.dec. No step decodes on
 * its own. A step applies its own ResultedDefect (or none) before
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
	/// The step's detection decoded; one that does not hands on to the next step.
	DecodeToWin,
	/// The step produced a valid detection at all: the plan ends there. It is
	/// still decoded here, once, so the step is won or a candidate on the spot
	/// -- the stock reader's rule, with the decode it used to do after the
	/// ladder moved inside it.
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
 * attribution hooks -- calls, time, skips, and which step won are all
 * observable right here (DMLadderStats.h; compiled out without
 * CRPT_ZXING_PROFILING).
 *
 * Per step, in order: skip it if its `when` says so; if the detection is
 * valid, remember its position and decode it once (unless a nested plan did);
 * under DecodeToWin return it if that decode is valid, under
 * FirstValidDetection return it either way. A step that did not end the plan
 * leaves its detection in ctx.last for the next one.
 *
 * `plan` names the plan and `base` is its array: a step is attributed to the
 * plan's name and its index in that array, so a reordered sequence still
 * reports each step under its own name.
 */
template <class Ctx>
DetectorResult RunSteps(const char* plan, const Step<Ctx>* base, const Step<Ctx>* const* first, const Step<Ctx>* const* last,
                        Ctx& ctx, Policy policy, Fallback fallback)
{
	for (; first != last; ++first) {
		const Step<Ctx>& step = **first;
		[[maybe_unused]] const size_t index = static_cast<size_t>(&step - base);
		if (step.when && !step.when(ctx)) {
			CRPT_LADDER_RECORD(plan, index, step.name, Outcome::Skipped);
			continue;
		}

		CRPT_LADDER_ENTER(plan, index, step.name);
		[[maybe_unused]] const auto t0 = CRPT_LADDER_CLOCK();
		bool decodedInside = false;
		DetectorResult det = step.run(ctx, decodedInside);
		[[maybe_unused]] Outcome outcome = Outcome::NoDetection;

		if (det.isValid()) {
			ctx.candidate = det.position();
			outcome = Outcome::Candidate;
			if (!decodedInside)
				ctx.dec = ctx.decode(det.bits());
			if (policy == Policy::FirstValidDetection) {
				CRPT_LADDER_LEAVE(ctx.dec.isValid() ? Outcome::Won : Outcome::Candidate, t0);
				return det;
			}
		}

		if (policy == Policy::DecodeToWin && ctx.dec.isValid()) {
			CRPT_LADDER_LEAVE(Outcome::Won, t0);
			return det;
		}

		CRPT_LADDER_LEAVE(outcome, t0);
		ctx.last = std::move(det);
	}

	if (fallback == Fallback::LastDetection)
		return std::move(ctx.last);
	return DetectorResult({}, std::move(ctx.candidate));
}

/// Run a plan in its built-in order. `name` is what attribution reports it
/// under: an entry plan's name starts every path, a sub-plan's is not in the path.
template <class Ctx, size_t N>
DetectorResult RunLadder(const char* name, const Step<Ctx> (&plan)[N], Ctx& ctx, Policy policy = Policy::DecodeToWin,
                         Fallback fallback = Fallback::EmptyWithCandidate)
{
	static_assert(N <= kMaxPlanSteps, "plan longer than kMaxPlanSteps");
	const Step<Ctx>* seq[N];
	for (size_t i = 0; i < N; ++i)
		seq[i] = &plan[i];
	return RunSteps(name, plan, seq, seq + N, ctx, policy, fallback);
}

/**
 * Run a plan in the order a Spec names, or its built-in order when the Spec
 * names no steps. A name the plan does not have is a hard error: a typo in a
 * schedule fails the run rather than silently running something else.
 */
template <class Ctx, size_t N>
DetectorResult RunLadder(const char* name, const Step<Ctx> (&plan)[N], const Spec& spec, Ctx& ctx, Policy policy = Policy::DecodeToWin,
                         Fallback fallback = Fallback::EmptyWithCandidate)
{
	if (spec.steps.empty())
		return RunLadder(name, plan, ctx, policy, fallback);
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
	return RunSteps(name, plan, seq, seq + n, ctx, policy, fallback);
}

} // namespace ZXing::DataMatrix::ladder
