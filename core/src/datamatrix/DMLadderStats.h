/*
* Copyright 2026 CRPT
*/
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The recording side of ladder attribution: what ladder::RunSteps calls, and
// the two notes a detector or a reader adds. The reading side is the C API in
// CrptLadderStats.h.
//
// Every macro here expands to nothing unless CRPT_ZXING_PROFILING is defined,
// so a shipping libZXing carries no clock reads, no table and no calls.

#include "CrptLadderStats.h"

#include <cstddef>
#include <cstdint>

#ifdef CRPT_ZXING_PROFILING
#include <chrono>
#endif

namespace ZXing::DataMatrix::ladder {

/// How one run of a step ended, as the runner sees it.
enum class Outcome : uint8_t
{
	Skipped,     ///< its `when` rule said no
	NoDetection, ///< ran, nothing valid came back
	Candidate,   ///< a valid detection that did not decode
	Won,         ///< decoded
	/// A valid detection under FirstValidDetection: the caller decodes it, so
	/// won or candidate is only known once the caller settles it.
	Detected,
};

/// Something a detector knows about the step hosting it that the runner does not.
enum class Note : uint8_t
{
	Bail, ///< gave up in setup, before any of its own variants ran
};

#ifdef CRPT_ZXING_PROFILING

namespace stats {

using TimePoint = std::chrono::steady_clock::time_point;

inline TimePoint Now() noexcept
{
	return std::chrono::steady_clock::now();
}

/// A step that was not run (its rule said no).
void Record(const char* plan, size_t index, const char* step, Outcome outcome) noexcept;

/// A step being run: pushes its path frame on construction. leave() records the
/// outcome and pops; if the step throws instead, the destructor pops without
/// recording, so the next decode on this thread starts from a clean path.
class Frame
{
public:
	Frame(const char* plan, size_t index, const char* step) noexcept;
	~Frame();
	Frame(const Frame&) = delete;
	Frame& operator=(const Frame&) = delete;

	void leave(Outcome outcome, TimePoint t0) noexcept;

private:
	bool _open = true;
};

/// Attach a note to the innermost open step.
void AddNote(Note note) noexcept;

/// A named piece of work inside a step -- the white-rect search, the 45-degree
/// rotation, the transition measurements. Times its scope and charges it to the
/// hosting step under "<step path>#<name>". `name` must be a string literal:
/// it is the probe's identity, registered process-wide on first use.
class Probe
{
public:
	explicit Probe(const char* name) noexcept;
	~Probe();
	Probe(const Probe&) = delete;
	Probe& operator=(const Probe&) = delete;

private:
	const char* _name;
	TimePoint _t0;
};

/// One pass of ReadBarcode.cpp's binarization walk: which binarizer, which
/// pyramid layer (0 = full resolution), closed, inverted. The walk tells it how
/// many results the pass added (addResults), so the walk row says which pass
/// produced the result that came back -- for any format, not only DataMatrix.
class Pass
{
public:
	Pass(int binarizer, int layer, bool close, bool invert) noexcept;
	~Pass();
	Pass(const Pass&) = delete;
	Pass& operator=(const Pass&) = delete;

	void addResults(uint32_t n) noexcept { _results += n; }

private:
	int _slot; // -1: no slot (a layer or binarizer beyond the table)
	uint32_t _results = 0;
	TimePoint _t0;
};

/// The caller's verdict on the detection parked by the last Detected leave.
void Settle(bool decoded) noexcept;

} // namespace stats

#define CRPT_LADDER_CLOCK() ::ZXing::DataMatrix::ladder::stats::Now()
#define CRPT_LADDER_RECORD(plan, index, step, outcome) ::ZXing::DataMatrix::ladder::stats::Record(plan, index, step, outcome)
#define CRPT_LADDER_ENTER(plan, index, step) ::ZXing::DataMatrix::ladder::stats::Frame crptLadderFrame_(plan, index, step)
#define CRPT_LADDER_LEAVE(outcome, t0) crptLadderFrame_.leave(outcome, t0)
#define CRPT_LADDER_NOTE(note) ::ZXing::DataMatrix::ladder::stats::AddNote(::ZXing::DataMatrix::ladder::Note::note)
#define CRPT_LADDER_PROBE_CAT_(a, b) a##b
#define CRPT_LADDER_PROBE_CAT(a, b) CRPT_LADDER_PROBE_CAT_(a, b)
#define CRPT_LADDER_PROBE(name) \
	::ZXing::DataMatrix::ladder::stats::Probe CRPT_LADDER_PROBE_CAT(crptLadderProbe_, __LINE__)(name)
#define CRPT_LADDER_SETTLE(decoded) ::ZXing::DataMatrix::ladder::stats::Settle(decoded)
#define CRPT_LADDER_PASS(binarizer, layer, close, invert) \
	::ZXing::DataMatrix::ladder::stats::Pass crptLadderPass_(static_cast<int>(binarizer), static_cast<int>(layer), close, invert)
/// After the pass's body: credit it with the valid results it appended to `results` past index `before`.
#define CRPT_LADDER_PASS_RESULTS(results, before) \
	do { \
		uint32_t crptLadderN_ = 0; \
		for (size_t crptLadderI_ = (before); crptLadderI_ < (results).size(); ++crptLadderI_) \
			crptLadderN_ += (results)[crptLadderI_].isValid() ? 1 : 0; \
		crptLadderPass_.addResults(crptLadderN_); \
	} while (0)

#else // !CRPT_ZXING_PROFILING

#define CRPT_LADDER_CLOCK() 0
#define CRPT_LADDER_RECORD(plan, index, step, outcome) ((void)0)
#define CRPT_LADDER_ENTER(plan, index, step) ((void)0)
#define CRPT_LADDER_LEAVE(outcome, t0) ((void)0)
#define CRPT_LADDER_NOTE(note) ((void)0)
#define CRPT_LADDER_PROBE(name) ((void)0)
#define CRPT_LADDER_SETTLE(decoded) ((void)0)
#define CRPT_LADDER_PASS(binarizer, layer, close, invert) ((void)0)
#define CRPT_LADDER_PASS_RESULTS(results, before) ((void)0)

#endif // CRPT_ZXING_PROFILING

} // namespace ZXing::DataMatrix::ladder
