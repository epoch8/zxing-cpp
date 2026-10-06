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
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ZXing::DataMatrix {
struct CrptSetup; // DMDetector.cpp: the product of the locator crptWhiteRect
}

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

// ---- Which Sampling a step uses: its own, unless a recipe overrides it -------

/// The names of the steps running right now, outermost first -- "crpt",
/// "lmarker[0]" while DetectCRPT's sub-plan runs its first L-marker variant.
/// Kept by the runner (StepScope) in every build, profiling or not: it is what
/// a recipe's per-step override is matched against, however deep the sampler
/// sits (Scan, inside DetectNew, inside a sub-plan step).
struct StepPath
{
	static constexpr size_t kDepth = 4;
	const char* names[kDepth] = {};
	size_t depth = 0; ///< may exceed kDepth; names beyond it are not kept
};

inline StepPath& CurrentStepPath() noexcept
{
	thread_local StepPath p;
	return p;
}

/// Pushes a step name for the duration of the step, and pops it even if the
/// step throws (the calculators catch decode exceptions and carry on).
class StepScope
{
public:
	explicit StepScope(const char* name) noexcept
	{
		StepPath& p = CurrentStepPath();
		if (p.depth < StepPath::kDepth)
			p.names[p.depth] = name;
		++p.depth;
	}
	~StepScope() { --CurrentStepPath().depth; }
	StepScope(const StepScope&) = delete;
	StepScope& operator=(const StepScope&) = delete;
};

/// A recipe's per-step choice of Sampling: `step` is a step name, or a
/// '/'-separated tail of a step path ("crptWarp/lmarker[0]"), matched against
/// the innermost steps running.
struct SamplingOverride
{
	const char* step = nullptr;
	const Sampling* sampling = nullptr;
};

struct SamplingOverrides
{
	static constexpr size_t kMax = 8;
	SamplingOverride items[kMax];
	size_t n = 0;
};

/// Installed per decode by the entry point from the recipe in force; empty
/// without one, which makes Pick return every step's own Sampling.
inline SamplingOverrides& CurrentSamplingOverrides() noexcept
{
	thread_local SamplingOverrides o;
	return o;
}

/// Does `key` ("lmarker[0]", "crptWarp/lmarker[0]") name the innermost steps of `path`?
inline bool MatchesStepPath(const char* key, const StepPath& path) noexcept
{
	const size_t kept = path.depth < StepPath::kDepth ? path.depth : StepPath::kDepth;
	if (kept != path.depth)
		return false; // deeper than kept: cannot tell, so no override
	std::string_view rest(key);
	size_t level = kept;
	while (!rest.empty()) {
		if (level == 0)
			return false;
		const size_t slash = rest.rfind('/');
		const std::string_view last = slash == std::string_view::npos ? rest : rest.substr(slash + 1);
		if (last != path.names[level - 1])
			return false;
		rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(0, slash);
		--level;
	}
	return true;
}

/// The Sampling a step should use: `own`, unless the recipe in force overrides
/// it for the steps running now.
inline const Sampling& Pick(const Sampling& own) noexcept
{
	const SamplingOverrides& o = CurrentSamplingOverrides();
	if (o.n == 0)
		return own;
	const StepPath& path = CurrentStepPath();
	for (size_t i = 0; i < o.n; ++i)
		if (MatchesStepPath(o.items[i].step, path))
			return *o.items[i].sampling;
	return own;
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
	/// The locator crptWhiteRect's product for `image`, computed by the plan's
	/// first DetectCRPT step and reused by the next one (crpt, then crptWarp):
	/// both locate on the same pixels, and the locator is a pure function of them.
	std::shared_ptr<CrptSetup> crptSetup;
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
/// A detection copied by hand: DetectorResult is move-only.
inline DetectorResult CopyOf(const DetectorResult& d)
{
	DetectorResult c(d.bits().copy(), QuadrilateralI(d.position()));
	c.setResultedDefect(d.resultedDefect());
	return c;
}

template <class Ctx>
DetectorResult RunSteps(const char* plan, const Step<Ctx>* base, const Step<Ctx>* const* first, const Step<Ctx>* const* last,
                        Ctx& ctx, Policy policy, Fallback fallback, bool sweepable = false)
{
	// Sweep (feature sweepSteps, exhaustive profiling runs only): a win does
	// not end the plan. The steps after it still run, on the state they would
	// have seen had the winner not ended the plan, so what each step can read
	// is recorded. The first win is what the plan returns, with the runner's
	// state put back as it was at that win: the result does not change.
	//
	// Entry plans only (`sweepable`, set by the RunLadder that takes a Spec).
	// A plan nested inside a step reports through its host's out-parameters
	// (DetectOldWithOffsets' correctedOffset, DetectCRPT's defect), which the
	// steps after its winner would overwrite and the runner cannot put back.
	const bool sweep = sweepable && ctx.on(Feat::sweepSteps);
	bool won = false;
	DetectorResult wonDet, wonLast;
	DecoderResult wonDec;
	QuadrilateralI wonCandidate;
	const auto hold = [&](DetectorResult&& det) {
		if (!won) {
			won = true;
			wonDec = std::move(ctx.dec);
			wonCandidate = ctx.candidate;
			wonLast = std::move(ctx.last);
			wonDet = std::move(det);
			ctx.last = CopyOf(wonDet);
		} else {
			ctx.last = std::move(det);
		}
		ctx.dec = DecoderResult();
	};

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
		DetectorResult det;
		{
			StepScope scope(step.name);
			det = step.run(ctx, decodedInside);
		}
		[[maybe_unused]] Outcome outcome = Outcome::NoDetection;

		if (det.isValid()) {
			ctx.candidate = det.position();
			outcome = Outcome::Candidate;
			if (!decodedInside)
				ctx.dec = ctx.decode(det.bits());
			if (policy == Policy::FirstValidDetection) {
				CRPT_LADDER_LEAVE(ctx.dec.isValid() ? Outcome::Won : Outcome::Candidate, t0);
				if (!sweep)
					return det;
				hold(std::move(det));
				continue;
			}
		}

		if (policy == Policy::DecodeToWin && ctx.dec.isValid()) {
			CRPT_LADDER_LEAVE(Outcome::Won, t0);
			if (!sweep)
				return det;
			hold(std::move(det));
			continue;
		}

		CRPT_LADDER_LEAVE(outcome, t0);
		ctx.last = std::move(det);
	}

	if (won) {
		ctx.dec = std::move(wonDec);
		ctx.candidate = wonCandidate;
		ctx.last = std::move(wonLast);
		return wonDet;
	}
	if (fallback == Fallback::LastDetection)
		return std::move(ctx.last);
	return DetectorResult({}, std::move(ctx.candidate));
}

/// Run a plan in its built-in order. `name` is what attribution reports it
/// under: an entry plan's name starts every path, a sub-plan's is not in the path.
template <class Ctx, size_t N>
DetectorResult RunLadder(const char* name, const Step<Ctx> (&plan)[N], Ctx& ctx, Policy policy = Policy::DecodeToWin,
                         Fallback fallback = Fallback::EmptyWithCandidate, bool sweepable = false)
{
	static_assert(N <= kMaxPlanSteps, "plan longer than kMaxPlanSteps");
	const Step<Ctx>* seq[N];
	for (size_t i = 0; i < N; ++i)
		seq[i] = &plan[i];
	return RunSteps(name, plan, seq, seq + N, ctx, policy, fallback, sweepable);
}

/**
 * Run a plan in the order a Spec names, or its built-in order when the Spec
 * names no steps. A name the plan does not have is a hard error: a typo in a
 * schedule fails the run rather than silently running something else.
 */
/// Run a plan in the order `order` names. A name the plan does not have is a
/// hard error. `sweepable`: see RunSteps.
template <class Ctx, size_t N>
DetectorResult RunLadderInOrder(const char* name, const Step<Ctx> (&plan)[N], const std::vector<std::string>& order, Ctx& ctx,
                                Policy policy, Fallback fallback, bool sweepable)
{
	if (order.size() > kMaxPlanSteps)
		throw std::invalid_argument("ladder spec names more than " + std::to_string(kMaxPlanSteps) + " steps");

	const Step<Ctx>* seq[kMaxPlanSteps];
	size_t n = 0;
	for (const std::string& stepName : order) {
		const Step<Ctx>* found = nullptr;
		for (const Step<Ctx>& step : plan)
			if (stepName == step.name) {
				found = &step;
				break;
			}
		if (!found)
			throw std::invalid_argument("unknown ladder step: " + stepName);
		seq[n++] = found;
	}
	return RunSteps(name, plan, seq, seq + n, ctx, policy, fallback, sweepable);
}

template <class Ctx, size_t N>
DetectorResult RunLadder(const char* name, const Step<Ctx> (&plan)[N], const Spec& spec, Ctx& ctx, Policy policy = Policy::DecodeToWin,
                         Fallback fallback = Fallback::EmptyWithCandidate)
{
	// An entry plan: the only kind a sweep runs through (RunSteps).
	if (spec.steps.empty())
		return RunLadder(name, plan, ctx, policy, fallback, /*sweepable=*/true);
	return RunLadderInOrder(name, plan, spec.steps, ctx, policy, fallback, /*sweepable=*/true);
}

/// The sub-plan orders of the decode in progress (Spec::subPlans), installed
/// by the entry point like CurrentFeatures. A copy, not a pointer into the
/// entry's spec, so nothing can dangle; empty in prod, where the copy is free.
inline std::vector<std::pair<std::string, std::vector<std::string>>>& CurrentSubPlans() noexcept
{
	thread_local std::vector<std::pair<std::string, std::vector<std::string>>> subPlans;
	return subPlans;
}

/// Run a plan nested inside a step: in the order the ladder string names for
/// it (`sub` -- "old", "crpt"), or its built-in order. Never swept (RunSteps).
template <class Ctx, size_t N>
DetectorResult RunSubLadder(const char* sub, const Step<Ctx> (&plan)[N], Ctx& ctx, Policy policy, Fallback fallback)
{
	for (const auto& [name, order] : CurrentSubPlans())
		if (name == sub)
			return RunLadderInOrder(sub, plan, order, ctx, policy, fallback, /*sweepable=*/false);
	return RunLadder(sub, plan, ctx, policy, fallback);
}

} // namespace ZXing::DataMatrix::ladder
