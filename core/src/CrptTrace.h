// Where DataMatrix detection gives up, printed to stderr. Version-independent
// on purpose: the newer zxing has CrptProfile counters and the older one has
// nothing, so a shared answer to "why did this buffer not decode" cannot be
// built on that. This needs only <cstdio>/<cstdlib>.
//
// Off unless CRPT_ZX_TRACE is set. One line per event:
//     ZXTRACE <tag> <a> <b>
#pragma once
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#include <vector>

namespace crpt_trace {
inline bool on() {
  static const bool v = std::getenv("CRPT_ZX_TRACE") != nullptr;
  return v;
}
// Frame id, pushed down from the MediaPipe calculator. ZXing has no concept of
// a frame, but without it two runs of the same clip cannot be paired: the crops
// differ by construction, so filenames alone cannot say "same frame, different
// offset" -- which is exactly the comparison needed.
inline long long& frame() { static thread_local long long f = -1; return f; }
inline void SetFrame(long long v) { frame() = v; }

// MUST be used from outside libZXing. SetFrame() above is an inline function
// with a static thread_local: the MediaPipe calculator and the shared library
// each get their OWN copy of that storage, so a setter called in the calculator
// never reaches the one ZXing reads (observed: every frame logged as -1).
// This one is defined once, inside the library, and exported.
void SetFrameExported(long long v);

inline void T(const char* tag, long a = -1, long b = -1) {
  if (!on()) return;
  std::fprintf(stderr, "ZXTRACE %s %ld %ld\n", tag, a, b);
}
// The decoded quadrilateral, in pixels of the buffer handed to the detector,
// alongside the buffer size. This is what distinguishes "it read the symbol"
// from "it locked onto some other rectangle in the crop" -- an empty-payload
// result cannot be attributed without it.
template <class Q>
inline void Quad(const char* tag, const Q& q, int w, int h) {
  if (!on()) return;
  std::fprintf(stderr, "ZXQUAD %s %d %d  %d,%d %d,%d %d,%d %d,%d\n", tag, w, h,
               q.topLeft().x, q.topLeft().y, q.topRight().x, q.topRight().y,
               q.bottomRight().x, q.bottomRight().y, q.bottomLeft().x,
               q.bottomLeft().y);
}

// Save the exact region a detector locked onto, as seen by the detector: the
// BINARIZED BitMatrix, not the pre-binarisation crop. Written as PGM so ZXing
// core needs no image-codec dependency.
//
// The whole matrix is written with the quad's bounding box marked by a 1px
// border, because the region alone cannot answer "is that the symbol or
// something else on the page" -- that needs the surrounding context.
// Off unless CRPT_ZX_REGION_DIR is set.
template <class M, class Q>
inline void Region(const char* tag, const M& m, const Q& q) {
  static const char* dir = std::getenv("CRPT_ZX_REGION_DIR");
  if (!dir) return;
  static std::atomic<long long> counter{0};
  const int w = m.width(), h = m.height();
  if (w <= 0 || h <= 0) return;
  int x0 = std::min(std::min(q.topLeft().x, q.topRight().x),
                    std::min(q.bottomLeft().x, q.bottomRight().x));
  int x1 = std::max(std::max(q.topLeft().x, q.topRight().x),
                    std::max(q.bottomLeft().x, q.bottomRight().x));
  int y0 = std::min(std::min(q.topLeft().y, q.topRight().y),
                    std::min(q.bottomLeft().y, q.bottomRight().y));
  int y1 = std::max(std::max(q.topLeft().y, q.topRight().y),
                    std::max(q.bottomLeft().y, q.bottomRight().y));
  char path[1024];
  std::snprintf(path, sizeof(path),
                "%s/zxreg_f%06lld__%08lld__%s__%dx%d__q%d_%d_%dx%d.pgm", dir,
                frame(), counter.fetch_add(1), tag, w, h, x0, y0, x1 - x0,
                y1 - y0);
  std::FILE* f = std::fopen(path, "wb");
  if (!f) return;
  std::fprintf(f, "P5\n%d %d\n255\n", w, h);
  std::vector<unsigned char> row(w);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      unsigned char v = m.get(x, y) ? 0 : 255;   // set bit = dark module
      const bool on_edge = (y == y0 || y == y1) ? (x >= x0 && x <= x1)
                                                : ((x == x0 || x == x1) && y > y0 && y < y1);
      row[x] = on_edge ? 128 : v;                // mid-grey outline = the quad
    }
    std::fwrite(row.data(), 1, w, f);
  }
  std::fclose(f);
}

// Same as Region(), but the filename also carries what the decode produced.
// Region() alone answers "where did it look"; this answers "and what came out",
// which is the join that lets a region set be filtered by outcome instead of
// inferred from aggregate counts.
template <class M, class Q>
inline void RegionPay(const char* tag, const M& m, const Q& q, long payload_bytes) {
  static const char* dir = std::getenv("CRPT_ZX_REGION_DIR");
  if (!dir) return;
  char t[128];
  std::snprintf(t, sizeof(t), "%s_%s%ld", tag,
                payload_bytes > 0 ? "READ" : "EMPTY", payload_bytes);
  Region(t, m, q);
}
}  // namespace crpt_trace
#define CRPT_T(...) ::crpt_trace::T(__VA_ARGS__)
#define CRPT_Q(...) ::crpt_trace::Quad(__VA_ARGS__)
#define CRPT_REGION(...) ::crpt_trace::Region(__VA_ARGS__)
#define CRPT_REGION_PAY(...) ::crpt_trace::RegionPay(__VA_ARGS__)
