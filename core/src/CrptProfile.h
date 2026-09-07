// Layer C instrumentation for libZXing — CRPT benchmarking only.
//
// WHY: Layer B (mediapipe/calculators/zxing/ladder_profile.h) times one
// `detector(...)` call as a single unit. Inside that call sits another ladder:
//
//   entry point (ReadBarcodes | readbarcodescrpt_samplegridv1)
//     -> loop over pyramid layers x close(0..1) x invert(0..1)   <- C2
//          -> DMCRPTReader::decode -> DetectSamplegridV1
//               -> up to 6 detector steps, first success wins    <- C5
//
// The loop does NOT early-exit: createHintsForLabel sets
// setMaxNumberOfSymbols(0xff) and `maxSymbols` only decrements when a UNIQUE
// result is found, so a failing decode walks every combination. C2 measures how
// much that actually costs.
//
// DESIGN — additive only. Nothing here changes the layout of an existing ZXing
// type. `Result`, `DecodeHints` and `DetectorResult` cross the libZXing.so
// boundary into mediapipe, whose vendored copies of these headers are resynced
// from this directory at build time (Dockerfile:200-210) precisely so both sides
// agree on layout. Adding a field to any of them risks a silent ABI mismatch —
// see ADR 0001 for how unpleasant that boundary already is. So: a new POD, two
// new exported functions, and nothing else.
//
// Counters are thread_local: MediaPipe may decode several detections
// concurrently, and a shared global would interleave them.
//
// GATE: CRPT_ZXING_PROFILING. Undefined (the default) makes every increment an
// empty inline no-op and the two functions zero-fill / do nothing, so the
// shipping libZXing.so is unchanged. Enable with
//   cmake -DCRPT_ZXING_PROFILING=ON ...
#ifndef ZXING_CRPT_PROFILE_H_
#define ZXING_CRPT_PROFILE_H_

#include <cstdint>

extern "C" {

// Aggregate counters since the last CrptProfileReset(), for the calling thread.
struct CrptProfileCounters {
	uint64_t entry_calls;       // ReadBarcodes + readbarcodescrpt_samplegridv1
	uint64_t loop_iters;        // pyramid layer x close x invert iterations
	uint64_t detect_calls;      // DetectSamplegridV1 step invocations (C5)
	uint64_t decode_calls;      // Decode()/DecodeResult() invocations
	uint64_t whiterect_calls;   // DetectWhiteRect from DetectSamplegridV1
	uint64_t whiterect_ns;      // ...and what it costs (its result is discarded)

	// C5 with TIMING, not just counts. Decode is ~73% of ladder time (Layer B),
	// and DetectSamplegridV1 is a 6-step ladder in its own right — counts alone
	// cannot say which step is expensive.
	uint64_t det_old_calls,    det_old_ns;      // DetectOldWithOffsets
	uint64_t det_crpt_calls,   det_crpt_ns;     // DetectCRPT (both plain + warp)
	uint64_t det_new_calls,    det_new_ns;      // DetectNew  (both plain + warp)
	uint64_t decode_res_calls, decode_res_ns;   // DecodeResult / Reed-Solomon (C7)
	// C1/C3 — cost paid per entry call before any detection happens.
	uint64_t binarizer_calls,  binarizer_ns;    // CreateBitmap + getBitMatrix
	uint64_t lum_calls,        lum_ns;          // SetupLumImageView ONLY (see pyramid_ns)

	// Splitting DetectCRPT. It is not a detector — it is a RETRY HARNESS that
	// transforms the image and calls DetectNew up to 6x (DMDetector.cpp:2275,2310).
	// Those nested calls were previously folded into det_crpt_ns, making
	// "DetectCRPT 548us vs DetectNew 23us" a comparison of a whole against one of
	// its parts. These separate retry-detection from image manipulation.
	uint64_t det_new_nested_calls, det_new_nested_ns;  // DetectNew INSIDE DetectCRPT
	uint64_t crpt_setup_calls,  crpt_setup_ns;   // copy + DetectWhiteRect(+rot45) + transitions
	uint64_t crpt_line_calls,   crpt_line_ns;    // copyTo + line3 x2 (L-marker synthesis)
	uint64_t crpt_resize_calls, crpt_resize_ns;  // cv::resize + cv::threshold
	uint64_t crpt_bottle_calls, crpt_bottle_ns;  // correctBottleCv (de-warp) per variant
	uint64_t crpt_decode_calls, crpt_decode_ns;  // DecodeResult inside DetectCRPT

	// Closing the accounting gap. 67% of DetectCRPT calls return early at the
	// white-rect stage, before the setup timer stops, so their cost was only
	// derivable by subtraction (~46% of DetectCRPT). These measure it directly
	// and separate the pieces that actually dominate.
	uint64_t crpt_bail_calls,  crpt_bail_ns;    // entry -> early return (no white rect)
	// Pair check (DMDetector.cpp:2536 vs 2576). DetectSamplegridV1 calls
	// DetectCRPT twice on the SAME image, differing only in warp params that are
	// first read at 2277 -- 31 lines AFTER the bail returns at 2246. So if call 1
	// bails, call 2 must bail too, having redone an identical rotateCV45.
	//   pair_checked  = times call 1 bailed and call 2 then ran
	//   pair_disagree = of those, times call 2 did NOT bail  <-- MUST STAY 0
	uint64_t crpt_pair_checked, crpt_pair_disagree;
	uint64_t crpt_copy_calls,  crpt_copy_ns;    // image.copy() at entry
	uint64_t crpt_wr_calls,    crpt_wr_ns;      // DetectWhiteRect INSIDE DetectCRPT
	uint64_t crpt_rot_calls,   crpt_rot_ns;     // rotateCV45 retry
	uint64_t crpt_trans_calls, crpt_trans_ns;   // 4x TransitionsBetween + sort
	// Whole-function timer at DetectCRPT's entry. det_crpt_ns only covers the TWO
	// call sites inside DetectSamplegridV1, but DetectCRPT has 7 call sites in
	// this file — so bail/setup counts (which fire for every caller) exceeded it,
	// giving a nonsensical "149% of DetectCRPT". This is the honest denominator.
	uint64_t crpt_total_calls, crpt_total_ns;

	// ---- Tier 1: whole-function self-timers -------------------------------
	// det_new_ns / det_crpt_ns only wrap DetectSamplegridV1's own call sites.
	// The ZXingStandard path reaches the SAME detectors through Detect(), and
	// the unwarp-predefined path through DetectDefined -- neither was counted
	// anywhere, so det_new_ns systematically undercounted DetectNew. These are
	// scoped at each function's entry, so they cover every caller.
	// NESTED, NOT ADDITIVE: new_total is inside det_new / det_new_nested;
	// detect_entry contains det_pure + new_total + crpt_total. Compare them,
	// do not sum them.
	uint64_t new_total_calls,   new_total_ns;     // DetectNew, all call sites
	uint64_t det_pure_calls,    det_pure_ns;      // DetectPure
	uint64_t det_defined_calls, det_defined_ns;   // DetectDefined (unwarp corners)
	uint64_t detect_entry_calls, detect_entry_ns; // Detect() = ZXingStandard's DM path

	// ---- Tier 1: DetectOldWithOffsets internals ---------------------------
	// Step 1 of DetectSamplegridV1, so it runs on EVERY detection, and was a
	// 191-line black box behind det_old_ns. Nested inside det_old_ns.
	uint64_t old_wr_calls,     old_wr_ns;       // DetectWhiteRect at entry
	uint64_t old_trans_calls,  old_trans_ns;    // 4x TransitionsBetween + sort, + 2x dimension
	uint64_t old_grid_calls,   old_grid_ns;     // SampleGrid, the 4 offset variants
	uint64_t old_gridtest_calls, old_gridtest_ns; // SampleGridTestOffseted (final)
	uint64_t old_ctr_calls,    old_ctr_ns;      // CorrectTopRight
	uint64_t old_decode_calls, old_decode_ns;   // DecodeResult inside DetectOldWithOffsets

	// ---- Tier 2: per-entry / per-layer costs that had no timer ------------
	// pyramid: the header used to claim lum_ns covered "SetupLumImageView +
	// pyramid". It never did -- LumImagePyramid is constructed AFTER the lum
	// scope closes, and it downscales the whole image once per layer.
	uint64_t pyramid_calls, pyramid_ns;   // LumImagePyramid construction
	uint64_t close_calls,   close_ns;     // BinaryBitmap::close(), morphological
	uint64_t invert_calls,  invert_ns;    // BinaryBitmap::invert()
	// DMDecoder::Decode() reached from DMReader (Reader::decode and the
	// DMCRPTReader fallback). decode_res_ns only covers DecodeResult() calls
	// made from inside DMDetector, so this path was invisible.
	uint64_t dm_decode_calls, dm_decode_ns;

	// WHICH VARIANT WON. Each of these loops tries several variants and returns
	// on the first that decodes, but they all share ONE timing counter, so the
	// profile shows what the loop COST and never which arm earned it. A variant
	// that never wins across a corpus is provably removable; without these you
	// cannot tell it apart from one that wins constantly.
	//   line   : L-marker synthesis, i=0 -> (n1,n2)=(0,1), i=1 -> (0,2)
	//   bottle : correctBottleCv, i encodes (i & 0b10, i & 0b01)
	//   grid   : DetectOldWithOffsets' four sequential SampleGrid attempts
	uint64_t crpt_line_win0, crpt_line_win1;
	uint64_t crpt_bottle_win0, crpt_bottle_win1, crpt_bottle_win2, crpt_bottle_win3;
	uint64_t old_grid_win0, old_grid_win1, old_grid_win2, old_grid_win3;

	// rotateCV45 is a RETRY, not a decode variant: DetectWhiteRect runs on the
	// unrotated crop first and the 45deg rotation only happens when that fails.
	// So "how often does rotate run" (crpt_rot_calls) answers nothing on its own
	// -- it counts failures of the first attempt. These two say whether the
	// retry is worth its ~700us:
	//   rescued : rotation FOUND a white rect the unrotated pass missed
	//   win     : ...and that rescued call went on to actually decode
	// rescued without win means the rotation buys candidates that never pay off.
	uint64_t crpt_rot_rescued, crpt_rot_win;
};

// Zero the calling thread's counters.
void CrptProfileReset();
// Copy the calling thread's counters out. Zero-fills when built without
// CRPT_ZXING_PROFILING, so a caller cannot tell the difference except that
// everything reads 0.
void CrptProfileSnapshot(struct CrptProfileCounters* out);

}  // extern "C"

#ifdef CRPT_ZXING_PROFILING

#include <chrono>

namespace ZXing {
namespace crpt {

CrptProfileCounters& Counters();

// Times a scope into `field`, e.g. the discarded DetectWhiteRect call.
class ScopedNs {
public:
	ScopedNs(uint64_t CrptProfileCounters::*field, uint64_t CrptProfileCounters::*count)
		: field_(field), count_(count), start_(std::chrono::steady_clock::now()) {}
	~ScopedNs() {
		Counters().*field_ += static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - start_).count());
		Counters().*count_ += 1;
	}
private:
	uint64_t CrptProfileCounters::*field_;
	uint64_t CrptProfileCounters::*count_;
	std::chrono::steady_clock::time_point start_;
};

}  // namespace crpt
}  // namespace ZXing

#define CRPT_ZX_COUNT(field) (::ZXing::crpt::Counters().field += 1)
// Explicit start/stop, for timing a region whose declarations must remain in the
// enclosing scope (a braced ScopedNs would scope them away).
#define CRPT_ZX_T0(name) auto name = std::chrono::steady_clock::now()
#define CRPT_ZX_T1(name, ns_field, count_field)                                 \
	do {                                                                        \
		::ZXing::crpt::Counters().ns_field += static_cast<uint64_t>(            \
			std::chrono::duration_cast<std::chrono::nanoseconds>(               \
				std::chrono::steady_clock::now() - name).count());              \
		::ZXing::crpt::Counters().count_field += 1;                             \
	} while (0)
#define CRPT_ZX_CAT_(a, b) a##b
#define CRPT_ZX_CAT(a, b) CRPT_ZX_CAT_(a, b)
#define CRPT_ZX_SCOPED_NS(ns_field, count_field)                          \
	::ZXing::crpt::ScopedNs CRPT_ZX_CAT(_crpt_scoped_, __LINE__)(         \
		&CrptProfileCounters::ns_field, &CrptProfileCounters::count_field)

#else  // !CRPT_ZXING_PROFILING

#define CRPT_ZX_COUNT(field) ((void)0)
#define CRPT_ZX_SCOPED_NS(ns_field, count_field) ((void)0)
#define CRPT_ZX_T0(name) ((void)0)
#define CRPT_ZX_T1(name, ns_field, count_field) ((void)0)

#endif  // CRPT_ZXING_PROFILING

#endif  // ZXING_CRPT_PROFILE_H_
