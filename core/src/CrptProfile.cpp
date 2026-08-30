// See CrptProfile.h. The two entry points are exported unconditionally so that
// a mediapipe build linking against a NON-profiling libZXing.so still resolves
// them — it simply reads zeros. That keeps the two library variants
// link-compatible, which matters because they share a filename and SONAME
// (docs_ai/plans/2026-08-19-speed-benchmarking.md §8.6).

#include "CrptProfile.h"

#include <cstring>

#ifdef CRPT_ZXING_PROFILING

namespace ZXing {
namespace crpt {

CrptProfileCounters& Counters()
{
	thread_local CrptProfileCounters c{};
	return c;
}

}  // namespace crpt
}  // namespace ZXing

extern "C" void CrptProfileReset()
{
	std::memset(&::ZXing::crpt::Counters(), 0, sizeof(CrptProfileCounters));
}

extern "C" void CrptProfileSnapshot(struct CrptProfileCounters* out)
{
	if (out) *out = ::ZXing::crpt::Counters();
}

#else  // built without profiling — present but inert.

extern "C" void CrptProfileReset() {}

extern "C" void CrptProfileSnapshot(struct CrptProfileCounters* out)
{
	if (out) std::memset(out, 0, sizeof(CrptProfileCounters));
}

#endif  // CRPT_ZXING_PROFILING
