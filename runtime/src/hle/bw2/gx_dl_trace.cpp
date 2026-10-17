// Display-list diagnostics for GX__CallDisplayList (runtime/src/hle/gx/gx_dl.cpp).
//
// BW2_DL_TRACE_FRAME=N[,M...]  one "[dltrace]" line per display list called during
//                              Aurora frame N: route taken, scan flags, the Aurora draws it
//                              produced, and how the HLE vertex state differs between the
//                              list's entry and exit (gx_dl.cpp formats the diff).
// BW2_DL_ENTRY_LAYOUT=0        publish the list's exit state instead of its entry state
//                              (undoes patch 0011; experiment only).
// Exceptions the display-list path swallows are counted and reported once per frame
// regardless of the trace, as are the routes taken, every 300 frames.
#include "bw2_hle.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace aurora::gfx {
uint32_t current_frame() noexcept;
}

namespace {

const std::vector<uint32_t>& TraceFrames() {
    static const std::vector<uint32_t> frames = [] {
        std::vector<uint32_t> out;
        if (const char* v = std::getenv("BW2_DL_TRACE_FRAME")) {
            for (const char* p = v; *p != 0;) {
                char* end = nullptr;
                out.push_back(static_cast<uint32_t>(std::strtoul(p, &end, 10)));
                if (end == p) break;
                p = (*end == ',') ? end + 1 : end;
            }
        }
        return out;
    }();
    return frames;
}

constexpr int kRouteCount = 6;
const char* const kRouteNames[kRouteCount] = {"regs", "flat-verbatim", "flat-copy", "direct", "interp", "lyt"};
uint64_t g_routeCounts[kRouteCount]{};
uint32_t g_routeWindowStart = 0;

} // namespace

bool Bw2DlTraceFrame() {
    const auto& frames = TraceFrames();
    if (frames.empty()) return false;
    const uint32_t frame = aurora::gfx::current_frame();
    for (uint32_t f : frames) {
        if (f == frame) return true;
    }
    return false;
}

bool Bw2DlEntryLayoutEnabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("BW2_DL_ENTRY_LAYOUT");
        return v == nullptr || std::strcmp(v, "0") != 0;
    }();
    return enabled;
}

void Bw2DlTraceLine(const char* format, ...) {
    char line[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    std::fprintf(stderr, "[dltrace] f%u %s\n", aurora::gfx::current_frame(), line);
}

void Bw2DlCountRoute(int route) {
    if (route < 0 || route >= kRouteCount) return;
    ++g_routeCounts[route];
    const uint32_t frame = aurora::gfx::current_frame();
    if (frame - g_routeWindowStart < 300) return;
    std::fprintf(stderr, "[dltrace] frames %u-%u: lists by route", g_routeWindowStart, frame);
    for (int i = 0; i < kRouteCount; ++i) {
        std::fprintf(stderr, " %s=%llu", kRouteNames[i], static_cast<unsigned long long>(g_routeCounts[i]));
        g_routeCounts[i] = 0;
    }
    std::fputc('\n', stderr);
    g_routeWindowStart = frame;
}

void Bw2DlNoteSwallowedException(const char* where, const char* what) {
    static uint64_t total = 0;
    static uint32_t lastFrameLogged = UINT32_MAX;
    ++total;
    const uint32_t frame = aurora::gfx::current_frame();
    if (frame == lastFrameLogged) return;
    lastFrameLogged = frame;
    std::fprintf(stderr, "[dltrace] f%u swallowed exception #%llu in %s: %s\n", frame,
                 static_cast<unsigned long long>(total), where, what != nullptr ? what : "(unknown)");
}
