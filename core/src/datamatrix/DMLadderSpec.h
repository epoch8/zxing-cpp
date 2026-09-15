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
struct Spec
{
	std::vector<std::string> steps;
	uint64_t featureBits = 0; ///< values for the overridden features
	uint64_t featureMask = 0; ///< which features the string overrode

	Features apply(Features base) const noexcept
	{
		base.bits = (base.bits & ~featureMask) | (featureBits & featureMask);
		return base;
	}

	bool empty() const noexcept { return steps.empty() && featureMask == 0; }
};

/**
 * Grammar: tokens separated by spaces and/or commas. `+name` turns a feature
 * on and `-name` off; any other token names a step of the plan being run, and
 * the order of those tokens is the run order. An unknown feature name throws
 * std::invalid_argument here; an unknown step name throws when the plan runs,
 * because only the plan knows its steps. Never a silent no-op: a typo in a
 * schedule must fail the run, not quietly produce a build whose numbers mean
 * something else.
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

		if (tok[0] == '+' || tok[0] == '-') {
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
