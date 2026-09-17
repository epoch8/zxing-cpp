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
	std::mutex pathLock;
	std::string path[kSlots];
};

Names& N()
{
	static Names names{};
	return names;
}

// Per thread, and all-zero initialised so it costs no constructor: slots and
// pending are stored +1, with 0 meaning none.
struct ThreadState
{
	CrptLadderStepStat table[kSlots];
	int frame[kMaxDepth]; // slot + 1 of each open step, outermost first
	uint32_t depth;       // open steps, including any beyond kMaxDepth
	int pending;          // slot + 1 of a Detected run awaiting Settle
	uint64_t pendingNs;
	int64_t pendingT0;
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
	case Outcome::Detected: ++s.candidate; break; // only reached through a flush
	case Outcome::Won:
		++s.calls;
		++s.won;
		s.nsWon += ns;
		return;
	}
	++s.calls;
	s.nsLost += ns;
}

// A Detected run nobody settled counts as a candidate, timed to its detection.
void FlushPending() noexcept
{
	if (t.pending) {
		Add(t.pending - 1, Outcome::Candidate, t.pendingNs);
		t.pending = 0;
	}
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
	uint64_t ns = Since(t0);
	if (outcome != Outcome::Detected) {
		Add(slot, outcome, ns);
		return;
	}
	FlushPending();
	if (slot < 0) {
		++t.dropped;
		return;
	}
	t.pending = slot + 1;
	t.pendingNs = ns;
	t.pendingT0 = t0.time_since_epoch().count();
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

void Settle(bool decoded) noexcept
{
	if (!t.pending)
		return;
	TimePoint t0{TimePoint::duration(t.pendingT0)};
	Add(t.pending - 1, decoded ? Outcome::Won : Outcome::Candidate, Since(t0));
	t.pending = 0;
}

} // namespace ZXing::DataMatrix::ladder::stats

using namespace ZXing::DataMatrix::ladder::stats;

extern "C" void CrptLadderReset(void)
{
	std::memset(t.table, 0, sizeof(t.table));
	t.pending = 0;
	t.dropped = 0;
}

extern "C" size_t CrptLadderSnapshot(struct CrptLadderStepStat* out, size_t n)
{
	if (!out)
		return 0;
	FlushPending();
	size_t k = n < kSlots ? n : kSlots;
	std::memcpy(out, t.table, k * sizeof(*out));
	return k;
}

extern "C" const char* CrptLadderNames(size_t i)
{
	if (i >= kSlots)
		return nullptr;
	Names& names = N();
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
