/*
* Copyright 2026 CRPT
*/
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Feature flags and the ladder string that carries them. Standard library
// only, on purpose: DecodeHints holds the raw string and this header is what
// turns it into something a plan can run, so nothing detector-side leaks into
// the public hints and the parser can be exercised without a zxing build.

#include <cstdint>
#include <iterator>
#include <utility>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ZXing::DataMatrix::ladder {

/// One value per registry line, in registry order.
enum class Feat : uint8_t
{
#define CRPT_FEATURE(name, deflt) name,
#include "CrptFeatures.def"
#undef CRPT_FEATURE
	kCount
};
static_assert(static_cast<unsigned>(Feat::kCount) <= 64, "features fit one word; widen Features::bits");

/// A set of feature values. `on()` is a single mask test; keep it that way,
/// it sits on paths that run millions of times per corpus.
struct Features
{
	uint64_t bits = 0;

	bool on(Feat f) const noexcept { return (bits >> static_cast<unsigned>(f)) & 1u; }

	Features& set(Feat f, bool v) noexcept
	{
		const uint64_t m = uint64_t{1} << static_cast<unsigned>(f);
		bits = v ? (bits | m) : (bits & ~m);
		return *this;
	}

	/// The registry defaults.
	static Features Defaults() noexcept
	{
		Features r;
#define CRPT_FEATURE(name, deflt) r.set(Feat::name, deflt);
#include "CrptFeatures.def"
#undef CRPT_FEATURE
		return r;
	}

	static const char* Name(Feat f) noexcept
	{
		static constexpr const char* names[] = {
#define CRPT_FEATURE(name, deflt) #name,
#include "CrptFeatures.def"
#undef CRPT_FEATURE
		};
		return names[static_cast<unsigned>(f)];
	}

	static bool Lookup(std::string_view name, Feat& out) noexcept
	{
		for (unsigned i = 0; i < static_cast<unsigned>(Feat::kCount); ++i)
			if (name == Name(static_cast<Feat>(i))) {
				out = static_cast<Feat>(i);
				return true;
			}
		return false;
	}
};

/// A parsed ladder string. Empty `steps` means the plan's built-in order;
/// the feature fields are overrides applied on top of whatever the caller's
/// hints set.
/// One pass of the binarization walk (ReadBarcode.cpp): a pyramid layer,
/// binarized, then closed if `close`, then inverted if `invert`.
struct WalkPass
{
	uint8_t layer = 0;
	bool close = false;
	bool invert = false;
};

/// The binarizers' names, in ZXing::Binarizer's order (DecodeHints.h). This
/// header stays standard-library only, so it names them rather than include it.
inline constexpr const char* kBinarizerNames[] = {"LocalAverage", "GlobalHistogram", "FixedThreshold", "BoolCast"};

/// The preprocessing ops' names, in ZXing::PreprocessOp's order (Preprocess.h),
/// and whether each keeps the image's size and geometry. All may run inside
/// the walk ("pre:"): one that does not keep geometry is a scale, a crop or a
/// rotation, whose affine map places results back on the layer
/// (PreprocessGeometry.h, ADR 0006 step 8).
inline constexpr struct
{
	const char* name;
	bool keepsGeometry;
	int maxArgs; ///< "pre:Name(a:b:c)" -- see ReadBarcode.cpp, PreParams, for what each means
} kPreprocessOps[] = {
	{"Original", true, 0},          {"AdaptiveThreshold", true, 0}, {"UnsharpMask", true, 0},
	{"AdaptiveMean", true, 0},      {"BlurThreshold", true, 0},     {"BlurThreshold2", true, 0},
	{"ScaledUnsharp", false, 0},    {"OtsuInverted", true, 0},      {"HistogramSharpening", true, 0},
	{"Morphological", true, 0},     {"BlackEnhancement", true, 0},  {"BoostWhite", true, 0},
	{"CLAHE", true, 0},             {"NormalizeEx", true, 0},       {"SauvolaBinarization", true, 3},
	{"ClaheEnhancement", true, 2},  {"TopHatTransformation", true, 1}, {"BlackHatTransformation", true, 1},
	{"AnisotropicDiffusion", true, 3}, {"SuperResolution", false, 1}, {"Erode", true, 2},
	{"Rotate", false, 1},           {"CenterCrop", false, 1},
};

/// One "pre:" op: its ZXing::PreprocessOp value and the arguments the token gave.
struct PreOp
{
	int op = 0;
	int nargs = 0;
	double args[3] = {0, 0, 0};
};

struct Spec
{
	std::vector<std::string> steps;
	std::string recipe;       ///< a named recipe (DMDetector.cpp, kRecipes), or empty
	uint64_t featureBits = 0; ///< values for the overridden features
	uint64_t featureMask = 0; ///< which features the string overrode
	/// The binarization walk, when the string sets one ("walk:", "bin:"); it
	/// runs around the plan, not inside it, so it is not part of empty().
	std::vector<WalkPass> walk; ///< explicit passes in order; empty = the built-in walk
	int binarizer = -1;         ///< a ZXing::Binarizer value; -1 = the hints' binarizer
	std::vector<PreOp> preOps;  ///< "pre:": ops run on each layer before it is binarized
	/// Orders for the plans nested inside steps ("old:grid[0]/testOffsetGrid",
	/// "crpt:lmarker[0]/bottlePrepare/bottle[0]"): sub-plan name -> its steps in
	/// run order. A sub-plan not named runs its built-in order. Not part of
	/// empty(): the entry plan's own order is unaffected.
	std::vector<std::pair<std::string, std::vector<std::string>>> subPlans;

	const std::vector<std::string>* subPlan(std::string_view name) const noexcept
	{
		for (const auto& [n, steps] : subPlans)
			if (n == name)
				return &steps;
		return nullptr;
	}

	bool hasWalk() const noexcept { return !walk.empty() || binarizer >= 0 || !preOps.empty(); }

	Features apply(Features base) const noexcept
	{
		base.bits = (base.bits & ~featureMask) | (featureBits & featureMask);
		return base;
	}

	bool empty() const noexcept { return steps.empty() && featureMask == 0 && recipe.empty(); }
};

/// "L0", "L1+inv", "L0+close+inv" -> a pass. Layer 0..9; each modifier once.
inline WalkPass ParseWalkPass(std::string_view text)
{
	WalkPass p;
	if (text.size() < 2 || text[0] != 'L' || text[1] < '0' || text[1] > '9')
		throw std::invalid_argument("bad walk pass \"" + std::string(text) + "\": expected L<layer>[+close][+inv]");
	p.layer = static_cast<uint8_t>(text[1] - '0');
	std::string_view rest = text.substr(2);
	while (!rest.empty()) {
		if (rest.substr(0, 4) == "+inv" && !p.invert) {
			p.invert = true;
			rest.remove_prefix(4);
		} else if (rest.substr(0, 6) == "+close" && !p.close) {
			p.close = true;
			rest.remove_prefix(6);
		} else {
			throw std::invalid_argument("bad walk pass \"" + std::string(text) + "\": expected L<layer>[+close][+inv]");
		}
	}
	return p;
}

/**
 * Grammar: tokens separated by spaces and/or commas. `=name` names a recipe
 * (at most one); `+name` turns a feature on and `-name` off, on top of the
 * recipe's; any other token names a step of the plan being run, and the order
 * of those tokens is the run order -- replacing the recipe's, if it has one. An unknown feature name throws
 * std::invalid_argument here; an unknown step name throws when the plan runs,
 * because only the plan knows its steps. Never a silent no-op: a typo in a
 * schedule must fail the run, not quietly produce a build whose numbers mean
 * something else.
 *
 * Two tokens set the binarization walk around the plan (ReadBarcode.cpp)
 * instead of a step: `walk:L0/L0+inv/L1` -- exactly these passes, in this
 * order, '/'-separated -- and `bin:Name` -- the binarizer (LocalAverage,
 * GlobalHistogram, FixedThreshold, BoolCast). Each at most once.
 *
 * `pre:CLAHE/BoostWhite` runs preprocessing ops on each pyramid layer before
 * it is binarized, in order (ADR 0006 step 8). Ops that resize, crop or rotate
 * are allowed: their affine maps compose, and a result's position is mapped
 * back to the layer. An op takes arguments as `Name(a:b:c)` -- ':' because ','
 * separates tokens -- e.g. `Rotate(15)`, `CenterCrop(0.9)`,
 * `SauvolaBinarization(31:0.3:128)`. At most once.
 *
 * Any other `name:a/b/c` token orders a plan nested inside a step (ADR 0006
 * step 7): `old:` the grids of oldWithOffsets, `crpt:` the L-marker and bottle
 * variants of the CRPT steps. Which names exist, and their steps, only the
 * detector knows: ValidateDMLadderSpec checks them at graph open.
 */
inline Spec ParseSpec(std::string_view text)
{
	Spec spec;
	size_t i = 0;
	while (i < text.size()) {
		while (i < text.size() && (text[i] == ' ' || text[i] == ',' || text[i] == '\t' || text[i] == '\n'))
			++i;
		size_t j = i;
		while (j < text.size() && !(text[j] == ' ' || text[j] == ',' || text[j] == '\t' || text[j] == '\n'))
			++j;
		if (j == i)
			break;
		std::string_view tok = text.substr(i, j - i);
		i = j;

		if (tok.substr(0, 5) == "walk:") {
			if (!spec.walk.empty())
				throw std::invalid_argument("more than one walk: token");
			std::string_view passes = tok.substr(5);
			while (true) {
				const size_t slash = passes.find('/');
				spec.walk.push_back(ParseWalkPass(passes.substr(0, slash)));
				if (slash == std::string_view::npos)
					break;
				passes.remove_prefix(slash + 1);
			}
		} else if (tok.substr(0, 4) == "bin:") {
			if (spec.binarizer >= 0)
				throw std::invalid_argument("more than one bin: token");
			const std::string_view name = tok.substr(4);
			for (int b = 0; b < static_cast<int>(std::size(kBinarizerNames)); ++b)
				if (name == kBinarizerNames[b])
					spec.binarizer = b;
			if (spec.binarizer < 0)
				throw std::invalid_argument("unknown binarizer: " + std::string(tok) +
											" (LocalAverage, GlobalHistogram, FixedThreshold, BoolCast)");
		} else if (tok.substr(0, 4) == "pre:") {
			if (!spec.preOps.empty())
				throw std::invalid_argument("more than one pre: token");
			std::string_view rest = tok.substr(4);
			while (true) {
				const size_t slash = rest.find('/');
				const std::string_view item = rest.substr(0, slash);
				const size_t paren = item.find('(');
				const std::string_view name = item.substr(0, paren);
				PreOp pre;
				pre.op = -1;
				for (int i = 0; i < static_cast<int>(std::size(kPreprocessOps)); ++i)
					if (name == kPreprocessOps[i].name)
						pre.op = i;
				if (pre.op < 0)
					throw std::invalid_argument("unknown preprocessing op in pre: \"" + std::string(name) + "\"");
				if (paren != std::string_view::npos) {
					if (item.back() != ')')
						throw std::invalid_argument("pre: \"" + std::string(item) + "\" -- expected Name(a:b:c)");
					std::string_view args = item.substr(paren + 1, item.size() - paren - 2);
					while (!args.empty()) {
						const size_t colon = args.find(':');
						const std::string a(args.substr(0, colon));
						if (pre.nargs == 3 || pre.nargs >= kPreprocessOps[pre.op].maxArgs)
							throw std::invalid_argument("pre: " + std::string(name) + " takes at most " +
														std::to_string(kPreprocessOps[pre.op].maxArgs) + " argument(s)");
						size_t used = 0;
						double v = 0;
						try {
							v = std::stod(a, &used);
						} catch (...) {
							used = 0;
						}
						if (a.empty() || used != a.size())
							throw std::invalid_argument("pre: bad argument \"" + a + "\" in " + std::string(item));
						pre.args[pre.nargs++] = v;
						if (colon == std::string_view::npos)
							break;
						args.remove_prefix(colon + 1);
					}
				}
				spec.preOps.push_back(pre);
				if (slash == std::string_view::npos)
					break;
				rest.remove_prefix(slash + 1);
			}
		} else if (const size_t colon = tok.find(':'); colon != std::string_view::npos && colon > 0) {
			const std::string name(tok.substr(0, colon));
			for (const auto& sp : spec.subPlans)
				if (sp.first == name)
					throw std::invalid_argument("more than one " + name + ": token");
			std::vector<std::string> steps;
			std::string_view rest = tok.substr(colon + 1);
			while (true) {
				const size_t slash = rest.find('/');
				const std::string_view step = rest.substr(0, slash);
				if (step.empty())
					throw std::invalid_argument("empty step in " + std::string(tok));
				steps.emplace_back(step);
				if (slash == std::string_view::npos)
					break;
				rest.remove_prefix(slash + 1);
			}
			spec.subPlans.emplace_back(name, std::move(steps));
		} else if (tok[0] == '=') {
			if (tok.size() < 2)
				throw std::invalid_argument("empty ladder recipe name");
			if (!spec.recipe.empty())
				throw std::invalid_argument("more than one ladder recipe: =" + spec.recipe + " and " + std::string(tok));
			spec.recipe = std::string(tok.substr(1));
		} else if (tok[0] == '+' || tok[0] == '-') {
			Feat f;
			if (tok.size() < 2 || !Features::Lookup(tok.substr(1), f))
				throw std::invalid_argument("unknown ladder feature: " + std::string(tok));
			const uint64_t m = uint64_t{1} << static_cast<unsigned>(f);
			spec.featureMask |= m;
			if (tok[0] == '+')
				spec.featureBits |= m;
			else
				spec.featureBits &= ~m;
		} else {
			spec.steps.emplace_back(tok);
		}
	}
	return spec;
}

} // namespace ZXing::DataMatrix::ladder
