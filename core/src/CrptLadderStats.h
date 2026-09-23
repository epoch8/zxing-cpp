/*
* Copyright 2026 CRPT
*/
// SPDX-License-Identifier: Apache-2.0

// Per-step attribution of the DataMatrix ladder, read from outside libZXing.
//
// The ladder runner (datamatrix/DMLadder.h) records, for every step it runs,
// how the step ended and how long it took, into a fixed table keyed by where
// the step ran: entry plan / step / sub-plan step, e.g. "sgv1/crpt/bottle[2]".
// This header is how a caller on the other side of libZXing.so reads it. See
// mediapipe/docs_ai/README_DM_LADDER.md part 2.
//
// C only, on purpose. Nothing here touches Result, DetectorResult or
// DecodeHints -- the types whose layout both sides of the library boundary
// must agree on (ADR 0001) -- so the table can grow without that problem.
//
// The functions are exported whether or not libZXing was built with
// CRPT_ZXING_PROFILING. Without it they are inert: Snapshot returns 0, Names
// returns null, Dropped returns 0. The profiling and shipping libraries share a
// filename and SONAME, so a caller built against either links against both.
//
// Thread-local, like the decodes: a snapshot reads the calling thread's table.

#ifndef ZXING_CRPT_LADDER_STATS_H_
#define ZXING_CRPT_LADDER_STATS_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Entry plans x steps per plan x (the step itself + one per sub-plan step).
#define CRPT_LADDER_ENTRY_PLANS 4
#define CRPT_LADDER_PLAN_STEPS 16
#define CRPT_LADDER_STEP_SLOTS (CRPT_LADDER_ENTRY_PLANS * CRPT_LADDER_PLAN_STEPS * (1 + CRPT_LADDER_PLAN_STEPS))

// Probes: named pieces of work INSIDE a step, e.g. the white-rect search in
// DetectCRPT's setup. They are addressed by the step that hosts them plus a
// name -- "sgv1/crpt#whiteRect" -- and live in slots after the step slots, so
// one snapshot returns both and a reader needs no second call. A probe fired
// while a sub-plan step is open is charged to its top-level step.
#define CRPT_LADDER_PROBE_NAMES 8
#define CRPT_LADDER_PROBE_SLOTS (CRPT_LADDER_ENTRY_PLANS * CRPT_LADDER_PLAN_STEPS * CRPT_LADDER_PROBE_NAMES)

// Walk passes: ReadBarcode.cpp's WalkBinarizations runs its reader once per
// (pyramid layer x close x invert) pass of one binarizer, so one decode call can
// run the DataMatrix ladder several times, and the step rows sum those runs. A
// walk row names the pass -- "@LocalAverage:L0", "@LocalAverage:L0+inv",
// "@LocalAverage:L1+close" -- whatever the format. Layer 0 is full resolution;
// each further layer is downscaled by the hints' factor.
#define CRPT_LADDER_WALK_BINARIZERS 4 // the DecodeHints Binarizer enum
#define CRPT_LADDER_WALK_LAYERS 8
#define CRPT_LADDER_WALK_SLOTS (CRPT_LADDER_WALK_BINARIZERS * CRPT_LADDER_WALK_LAYERS * 2 * 2)

// Walk setup: the work ReadBarcodes does around its passes rather than in them
// -- "@setup#lum" (the input to luminance, once per call), "@setup#pyramid"
// (the downscaled layers, once per call), "@setup#close" and "@setup#invert"
// (the bitmap operations between passes). Probe rows: calls and nsLost only.
#define CRPT_LADDER_WALK_SETUP_SLOTS 4

#define CRPT_LADDER_SLOTS (CRPT_LADDER_STEP_SLOTS + CRPT_LADDER_PROBE_SLOTS + CRPT_LADDER_WALK_SLOTS + CRPT_LADDER_WALK_SETUP_SLOTS)

// One step, at one path, since the last CrptLadderReset().
struct CrptLadderStepStat
{
	uint32_t calls;       // ran (skipped steps are not calls)
	uint32_t skipped;     // its `when` rule said no
	uint32_t noDetection; // ran, nothing valid came back
	uint32_t candidate;   // a valid detection that did not decode
	uint32_t won;         // decoded -- the read is this step's
	uint32_t bailed;      // the detector it hosts gave up in setup (white rect, L sides, ...)
	uint64_t nsWon;       // time of the calls that won, decode included
	uint64_t nsLost;      // time of the calls that did not
};

// Three kinds of row share the struct; tell them apart by the path.
//
// STEP rows ("sgv1/crpt", "sgv1/crpt/lmarker[0]") use every field, and
// `calls == noDetection + candidate + won` holds for them.
//
// PROBE rows (path contains '#') use two fields: `calls` is how often the probe
// ran and `nsLost` is the total time it took. A probe has no outcome of its own;
// it is a piece of the step above it.
//
// WALK rows (path starts with '@') use four: `calls` is how many times the pass
// ran, `won` how many results it added to what the walk returns (valid ones,
// after the walk's dedup -- so a symbol decoded again on a later pass counts
// once, on the pass that found it first), and nsWon / nsLost the time of the
// passes that did / did not add one. That time is the pass's whole body: the
// reader, plus the binarization on a layer's first pass.

// Zero the calling thread's table.
void CrptLadderReset(void);

// Copy up to n slots of the calling thread's table into out; returns how many
// were written (CRPT_LADDER_SLOTS when n allows, 0 without profiling).
size_t CrptLadderSnapshot(struct CrptLadderStepStat* out, size_t n);

// The path of slot i ("sgv1/crpt/bottle[2]"), or null if no thread has touched
// that slot yet. Stable for the life of the process.
const char* CrptLadderNames(size_t i);

// Records that had no slot since the last reset: deeper than a sub-plan, or a
// plan or step beyond the table. Should stay 0; if it does not, the table is
// too small for the plans.
uint32_t CrptLadderDropped(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ZXING_CRPT_LADDER_STATS_H_
