/*
* Copyright 2026 CRPT
*/
// SPDX-License-Identifier: Apache-2.0

// The table behind CrptLadderStats.h. Without CRPT_ZXING_PROFILING only the
// inert C entry points below are compiled, so a caller links either way.

#include "DMLadderStats.h"

#include <cstring>

#ifdef CRPT_ZXING_PROFILING

#include <atomic>
#include <mutex>
#include <string>

namespace ZXing::DataMatrix::ladder::stats {
namespace {

constexpr size_t kEntries = CRPT_LADDER_ENTRY_PLANS;
constexpr size_t kSteps = CRPT_LADDER_PLAN_STEPS;
constexpr size_t kSpan = 1 + kSteps; // slots per (entry, step): itself + its sub-plan's steps
constexpr size_t kStepSlots = CRPT_LADDER_STEP_SLOTS;
constexpr size_t kProbeNames = CRPT_LADDER_PROBE_NAMES;
constexpr size_t kProbeSlots = CRPT_LADDER_PROBE_SLOTS;
constexpr size_t kWalkBase = kStepSlots + kProbeSlots;
constexpr size_t kWalkBinarizers = CRPT_LADDER_WALK_BINARIZERS;
constexpr size_t kWalkLayers = CRPT_LADDER_WALK_LAYERS;
constexpr size_t kSlots = CRPT_LADDER_SLOTS;
constexpr size_t kMaxDepth = 4;

// Process-wide, so a slot means the same path on every thread: which plan each
// entry index is, and the names a slot's path is built from. Written once each,
// on first touch; the paths themselves are only built when someone asks.
struct Names
{
	std::atomic<const char*> entry[kEntries];
	std::atomic<const char*> step[kEntries * kSteps];
	std::atomic<const char*> subPlan[kEntries * kSteps];
	std::atomic<const char*> subStep[kEntries * kSteps * kSteps];
	std::atomic<const char*> probe[kProbeNames]; // probe names are global: the same
	                                             // "whiteRect" in any step is one name
	std::mutex pathLock;
	std::string path[kSlots];
};

Names& N()
{
	static Names names{};
	return names;
}

// Per thread, and all-zero initialised so it costs no constructor: slots are
// stored +1, with 0 meaning none.
struct ThreadState
{
	CrptLadderStepStat table[kSlots];
	int frame[kMaxDepth]; // slot + 1 of each open step, outermost first
	uint32_t depth;       // open steps, including any beyond kMaxDepth
	uint32_t dropped;
	const char* lastPlan; // the last entry plan looked up, and its index + 1
	int lastEntry;
};

thread_local ThreadState t;

uint64_t Since(TimePoint t0) noexcept
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Now() - t0).count());
}

// Store `value` if nothing is stored yet; return what is stored afterwards.
const char* Claim(std::atomic<const char*>& slot, const char* value) noexcept
{
	const char* cur = slot.load(std::memory_order_acquire);
	if (!cur && !slot.compare_exchange_strong(cur, value))
		return cur;
	return cur ? cur : value;
}

bool Same(const char* a, const char* b) noexcept
{
	return a == b || std::strcmp(a, b) == 0;
}

int EntryIndex(const char* plan) noexcept
{
	if (t.lastPlan == plan)
		return t.lastEntry - 1;
	for (size_t i = 0; i < kEntries; ++i) {
		if (Same(Claim(N().entry[i], plan), plan)) {
			t.lastPlan = plan;
			t.lastEntry = static_cast<int>(i) + 1;
			return static_cast<int>(i);
		}
	}
	return -1;
}

// The slot step `index` of `plan` gets if it runs now, at the current depth,
// or -1: deeper than one sub-plan, a step beyond the table, a fifth entry plan,
// or a second, different sub-plan under a step that already hosts one.
int SlotFor(const char* plan, size_t index, const char* step) noexcept
{
	if (index >= kSteps)
		return -1;
	Names& n = N();
	if (t.depth == 0) {
		int entry = EntryIndex(plan);
		if (entry < 0)
			return -1;
		size_t es = static_cast<size_t>(entry) * kSteps + index;
		Claim(n.step[es], step);
		return static_cast<int>(es * kSpan);
	}
	if (t.depth == 1) {
		int host = t.frame[0] - 1;
		if (host < 0)
			return -1;
		size_t es = static_cast<size_t>(host) / kSpan;
		if (!Same(Claim(n.subPlan[es], plan), plan))
			return -1;
		Claim(n.subStep[es * kSteps + index], step);
		return static_cast<int>(es * kSpan + 1 + index);
	}
	return -1;
}

void Add(int slot, Outcome outcome, uint64_t ns) noexcept
{
	if (slot < 0) {
		++t.dropped;
		return;
	}
	CrptLadderStepStat& s = t.table[slot];
	switch (outcome) {
	case Outcome::Skipped: ++s.skipped; return;
	case Outcome::NoDetection: ++s.noDetection; break;
	case Outcome::Candidate: ++s.candidate; break;
	case Outcome::Won:
		++s.calls;
		++s.won;
		s.nsWon += ns;
		return;
	}
	++s.calls;
	s.nsLost += ns;
}

} // namespace

void Record(const char* plan, size_t index, const char* step, Outcome outcome) noexcept
{
	Add(SlotFor(plan, index, step), outcome, 0);
}

Frame::Frame(const char* plan, size_t index, const char* step) noexcept
{
	int slot = SlotFor(plan, index, step);
	if (t.depth < kMaxDepth)
		t.frame[t.depth] = slot + 1;
	++t.depth;
}

Frame::~Frame()
{
	if (_open)
		--t.depth;
}

void Frame::leave(Outcome outcome, TimePoint t0) noexcept
{
	_open = false;
	--t.depth;
	int slot = t.depth < kMaxDepth ? t.frame[t.depth] - 1 : -1;
	Add(slot, outcome, Since(t0));
}

namespace {

// The probe-name index, assigned process-wide on first use like an entry plan.
int ProbeIndex(const char* name) noexcept
{
	for (size_t i = 0; i < kProbeNames; ++i)
		if (Same(Claim(N().probe[i], name), name))
			return static_cast<int>(i);
	return -1;
}

// Probes are charged to the TOP-LEVEL step in the open path, so a probe inside
// a sub-plan step lands on the step that hosts the sub-plan.
int ProbeSlot(const char* name) noexcept
{
	if (t.depth == 0 || t.frame[0] <= 0)
		return -1;
	int probeIdx = ProbeIndex(name);
	if (probeIdx < 0)
		return -1;
	size_t es = static_cast<size_t>(t.frame[0] - 1) / kSpan; // (entry, step)
	return static_cast<int>(kStepSlots + es * kProbeNames + static_cast<size_t>(probeIdx));
}

} // namespace

namespace {

int WalkSlot(int binarizer, int layer, bool close, bool invert) noexcept
{
	if (binarizer < 0 || static_cast<size_t>(binarizer) >= kWalkBinarizers || layer < 0 ||
		static_cast<size_t>(layer) >= kWalkLayers)
		return -1;
	size_t i = ((static_cast<size_t>(binarizer) * kWalkLayers + static_cast<size_t>(layer)) * 2 + close) * 2 + invert;
	return static_cast<int>(kWalkBase + i);
}

} // namespace

Pass::Pass(int binarizer, int layer, bool close, bool invert) noexcept
	: _slot(WalkSlot(binarizer, layer, close, invert)), _t0(Now())
{
	if (_slot < 0)
		++t.dropped;
}

Pass::~Pass()
{
	if (_slot < 0)
		return;
	uint64_t ns = Since(_t0);
	CrptLadderStepStat& s = t.table[_slot];
	++s.calls;
	s.won += _results;
	(_results ? s.nsWon : s.nsLost) += ns;
}

Probe::Probe(const char* name) noexcept : _name(name), _t0(Now()) {}

Probe::~Probe()
{
	uint64_t ns = Since(_t0);
	int slot = ProbeSlot(_name);
	if (slot < 0) {
		++t.dropped;
		return;
	}
	++t.table[slot].calls;
	t.table[slot].nsLost += ns; // probe rows carry their total time here
}

void AddNote(Note note) noexcept
{
	int slot = (t.depth > 0 && t.depth <= kMaxDepth) ? t.frame[t.depth - 1] - 1 : -1;
	if (slot < 0) {
		++t.dropped;
		return;
	}
	switch (note) {
	case Note::Bail: ++t.table[slot].bailed; break;
	}
}

} // namespace ZXing::DataMatrix::ladder::stats

using namespace ZXing::DataMatrix::ladder::stats;

extern "C" void CrptLadderReset(void)
{
	std::memset(t.table, 0, sizeof(t.table));
	t.dropped = 0;
}

extern "C" size_t CrptLadderSnapshot(struct CrptLadderStepStat* out, size_t n)
{
	if (!out)
		return 0;
	size_t k = n < kSlots ? n : kSlots;
	std::memcpy(out, t.table, k * sizeof(*out));
	return k;
}

extern "C" const char* CrptLadderNames(size_t i)
{
	if (i >= kSlots)
		return nullptr;
	Names& names = N();
	if (i >= kWalkBase) { // a walk pass: "@<binarizer>:L<layer>[+close][+inv]"
		static const char* const kBinarizers[kWalkBinarizers] = {"LocalAverage", "GlobalHistogram",
																 "FixedThreshold", "BoolCast"};
		std::lock_guard<std::mutex> lock(names.pathLock);
		std::string& path = names.path[i];
		if (path.empty()) {
			size_t w = i - kWalkBase;
			bool invert = w % 2, close = (w / 2) % 2;
			size_t layer = (w / 4) % kWalkLayers, binarizer = w / 4 / kWalkLayers;
			path = std::string("@") + kBinarizers[binarizer] + ":L" + std::to_string(layer);
			if (close)
				path += "+close";
			if (invert)
				path += "+inv";
		}
		return path.c_str();
	}
	if (i >= kStepSlots) { // a probe: "<entry>/<step>#<probe>"
		size_t p = i - kStepSlots;
		size_t es = p / kProbeNames;
		const char* entry = names.entry[es / kSteps].load(std::memory_order_acquire);
		const char* step = names.step[es].load(std::memory_order_acquire);
		const char* probe = names.probe[p % kProbeNames].load(std::memory_order_acquire);
		if (!entry || !step || !probe)
			return nullptr;
		std::lock_guard<std::mutex> lock(names.pathLock);
		std::string& path = names.path[i];
		if (path.empty()) {
			path = entry;
			path += '/';
			path += step;
			path += '#';
			path += probe;
		}
		return path.c_str();
	}
	size_t es = i / kSpan;
	size_t sub = i % kSpan;
	const char* entry = names.entry[es / kSteps].load(std::memory_order_acquire);
	const char* step = names.step[es].load(std::memory_order_acquire);
	const char* subStep = sub ? names.subStep[es * kSteps + sub - 1].load(std::memory_order_acquire) : nullptr;
	if (!entry || !step || (sub && !subStep))
		return nullptr;
	std::lock_guard<std::mutex> lock(names.pathLock);
	std::string& path = names.path[i];
	if (path.empty()) {
		path = entry;
		path += '/';
		path += step;
		if (subStep) {
			path += '/';
			path += subStep;
		}
	}
	return path.c_str();
}

extern "C" uint32_t CrptLadderDropped(void)
{
	return t.dropped;
}

#else // !CRPT_ZXING_PROFILING -- present but inert

extern "C" void CrptLadderReset(void) {}

extern "C" size_t CrptLadderSnapshot(struct CrptLadderStepStat*, size_t)
{
	return 0;
}

extern "C" const char* CrptLadderNames(size_t)
{
	return nullptr;
}

extern "C" uint32_t CrptLadderDropped(void)
{
	return 0;
}

#endif // CRPT_ZXING_PROFILING
