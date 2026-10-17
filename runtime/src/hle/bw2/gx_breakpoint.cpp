// RBWE01 GX FIFO breakpoints.
//
// The game triple-buffers its GP FIFO: after queueing a frame it arms a FIFO
// breakpoint with GXEnableBreakPt and sleeps until GXCPInterruptHandler runs
// the callback registered with GXSetBreakPtCallback (0x8032C980), which picks
// the next buffer and wakes the main thread. Here the FIFO is consumed as it is
// written, so the GPU has always reached the breakpoint already; the callback
// is delivered at the next VI retrace, from the interrupt context the retrace
// path provides, as the CP interrupt would have done. No CP register is touched.
#include "bw2_hle.h"
#include "settings_overlay.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include "abi_bridge.h"
#include "fiber_manager.h"
#include "guest_interrupt_context.h"
#include "hle_stubs.h"
#include "memory.h"

namespace {

constexpr uint32_t kBreakPointCallbackAddr = 0x805FFD84u;  // -0x13DC(r13), GXSetBreakPtCallback
constexpr uint32_t kBreakPointAddr = 0x805FFD80u;          // -0x13E0(r13), Enable/DisableBreakPt

std::atomic<bool> g_breakPointPending{false};
std::atomic<uint16_t> g_lastDrawSyncToken{0};

} // namespace

extern "C" uint32_t GXSetBreakPtCallback_bw2(uint32_t callback) {
    const uint32_t previous = Memory::Read32(kBreakPointCallbackAddr);
    Memory::Write32(kBreakPointCallbackAddr, callback);
    return previous;
}
PPC_NATIVE_OVERRIDE(802AC2C4, GXSetBreakPtCallback_bw2, uint32_t, (uint32_t callback), (callback));

extern "C" void GXEnableBreakPt_bw2(uint32_t breakPoint) {
    Memory::Write32(kBreakPointAddr, breakPoint);
    g_breakPointPending.store(true, std::memory_order_release);
}
PPC_NATIVE_OVERRIDE_VOID(802AC308, GXEnableBreakPt_bw2, (uint32_t breakPoint), (breakPoint));

extern "C" void GXDisableBreakPt_bw2() {
    Memory::Write32(kBreakPointAddr, 0);
    g_breakPointPending.store(false, std::memory_order_release);
}
PPC_NATIVE_OVERRIDE_VOID(802AC3A8, GXDisableBreakPt_bw2, (), ());

namespace {
// BW2_TRACE_THREADS=1: every ~5 s (300 retraces) list the guest threads from the
// OS active-thread list (0x800000DC, linkActive.next at +0x2FC): state, priority,
// suspend count, the queue a waiting thread sleeps on, and its saved SRR0.
void TraceGuestThreads() {
    static const bool enabled = std::getenv("BW2_TRACE_THREADS") != nullptr;
    static uint32_t retraces = 0;
    if (!enabled || (++retraces % 300) != 0) {
        return;
    }
    std::fprintf(stderr, "[bw2-threads] current=%08X preRetrace=%08X postRetrace=%08X reschedule=%d runQueueBits=%08X runQueueHint=%u\n",
                 Memory::Read32(0x800000E4u), Memory::Read32(0x80600158u), Memory::Read32(0x80600154u), static_cast<int32_t>(Memory::Read32(0x805FFEF0u)),
                 Memory::Read32(0x805FFEF8u), Memory::Read32(0x805FFEF4u));
    {
        constexpr uint32_t p = 0x805BD7F0u;  // movie player state (temporary trace)
        std::fprintf(stderr, "[bw2-thp] aiCb=%08X b8=%08X bc=%02X bd=%02X be=%02X bf=%02X c0=%08X c4=%08X e0=%08X e4=%08X 108=%08X 10c=%08X a0=%08X\n",
                     Memory::Read32(0x805FFA90u), Memory::Read32(p + 0xB8), Memory::Read8(p + 0xBC), Memory::Read8(p + 0xBD), Memory::Read8(p + 0xBE),
                     Memory::Read8(p + 0xBF), Memory::Read32(p + 0xC0), Memory::Read32(p + 0xC4), Memory::Read32(p + 0xE0),
                     Memory::Read32(p + 0xE4), Memory::Read32(p + 0x108), Memory::Read32(p + 0x10C), Memory::Read32(p + 0xA0));
    }
    uint32_t thread = Memory::Read32(0x800000DCu);
    for (int i = 0; thread != 0 && i < 32; ++i) {
        std::fprintf(stderr, "[bw2-threads]   %08X state=%u prio=%d suspend=%d queue=%08X srr0=%08X lr=%08X\n",
                     thread, Memory::Read16(thread + 0x2C8u), static_cast<int32_t>(Memory::Read32(thread + 0x2D0u)),
                     static_cast<int32_t>(Memory::Read32(thread + 0x2CCu)), Memory::Read32(thread + 0x2DCu),
                     Memory::Read32(thread + 0x198u), Memory::Read32(thread + 0x84u));
        thread = Memory::Read32(thread + 0x2FCu);
    }
}
} // namespace

namespace aurora::gfx {
uint32_t current_frame() noexcept;
}
extern "C" void aurora_request_frame_capture(uint32_t frame, const char* outputPath);

namespace {
// BW2_CAPTURE_FRAMES=<dir>: every ~5 s ask Aurora to write the rendered frame to <dir>/frame_N.bmp
// (the image only exists on the GPU; this terminal cannot screen-capture the window).
void CaptureRenderedFrame() {
    static const char* dir = std::getenv("BW2_CAPTURE_FRAMES");
    static uint32_t retraces = 0;
    static int index = 0;
    if (dir == nullptr || (++retraces % 300) != 0) {
        return;
    }
    static char path[1024];
    std::snprintf(path, sizeof(path), "%s/frame_%03d.bmp", dir, index++);
    const uint32_t frame = aurora::gfx::current_frame() + 2;
    aurora_request_frame_capture(frame, path);
    std::fprintf(stderr, "[capture] frame=%u %s\n", frame, path);
}
} // namespace

void Bw2DeliverGxBreakpoint(CpuContext* ctx) {
    // The runtime's startup screen ends (and stops blocking input) when MKW's
    // StrapScene::CheckInput runs; BW2 has no such hook, so its first retrace does it.
    static bool strapAccepted = false;
    if (!strapAccepted) {
        strapAccepted = true;
        settings_overlay::NotifyStrapInputAccepted();
    }
    TraceGuestThreads();
    CaptureRenderedFrame();
    if (ctx == nullptr || !g_breakPointPending.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    const uint32_t callback = Memory::Read32(kBreakPointCallbackAddr);
    if (callback != 0) {
        InvokeIndirectCpu(callback, ctx);
    }
}

void Bw2RecordDrawSyncToken(unsigned int token) {
    g_lastDrawSyncToken.store(static_cast<uint16_t>(token), std::memory_order_release);
}

// GXReadDrawSync reads the PE token register through __peReg; every token the
// game sets with GXSetDrawSync has already been passed when it is read.
extern "C" uint32_t GXReadDrawSync_bw2() {
    return g_lastDrawSyncToken.load(std::memory_order_acquire);
}
PPC_NATIVE_OVERRIDE(802AD91C, GXReadDrawSync_bw2, uint32_t, (), ());

// GXGetCurrentGXThread is a one-load leaf the translator inlines. BW2's frame
// sync (0x8032E7E0) spins on it until its VI pre-retrace callback has swapped
// the last queued frame; on the console the retrace interrupt lands during the
// spin. As a native call it gives the spin the same chance GX drawing gets:
// due retraces and alarms are serviced as interrupts, at most once per ms.
namespace {
constexpr uint32_t kGxCurrentThreadAddr = 0x805FFD8Cu;  // -0x13D4(r13)
thread_local std::chrono::steady_clock::time_point g_nextSpinPoll{};
} // namespace

extern "C" uint32_t GXGetCurrentGXThread_bw2() {
    const auto now = std::chrono::steady_clock::now();
    if (now >= g_nextSpinPoll) {
        g_nextSpinPoll = now + std::chrono::milliseconds(1);
        if (Fiber::GuestFiberManager::IsInitialized()) {
            VI_HLE_ProcessRetracesDeferred(8);
            OS_HLE_ProcessAlarmsDeferred(8);
            Audio_HLE_PollDeferred();
        } else if (TryGetCpuContext() != nullptr) {
            GuestInterruptCallbackContext interrupt;
            VI_HLE_PollRetrace(interrupt.get());
        }
    }
    return Memory::Read32(kGxCurrentThreadAddr);
}
PPC_NATIVE_OVERRIDE(802AC658, GXGetCurrentGXThread_bw2, uint32_t, (), ());

// __GXData copy registers as GXSetDispCopySrc (+0x230/+0x234), GXSetTexCopySrc
// (+0x240/+0x244) and GXSetDispCopyDst (+0x238) store them in RBWE01.
namespace {
constexpr uint32_t kGXDataPtr = 0x806033F0u;
}

void Bw2MirrorCopySrc(bool display, unsigned int left, unsigned int top, unsigned int width, unsigned int height) {
    const uint32_t gx = Memory::Read32(kGXDataPtr);
    if (gx == 0) {
        return;
    }
    const uint32_t topLeft = (0x49u << 24) | ((top & 0x3FFu) << 10) | (left & 0x3FFu);
    const uint32_t size = (0x4Au << 24) | (((height - 1) & 0x3FFu) << 10) | ((width - 1) & 0x3FFu);
    Memory::Write32(gx + (display ? 0x230u : 0x240u), topLeft);
    Memory::Write32(gx + (display ? 0x234u : 0x244u), size);
}

void Bw2MirrorDispCopyDst(unsigned int width) {
    const uint32_t gx = Memory::Read32(kGXDataPtr);
    if (gx == 0) {
        return;
    }
    const uint32_t stride = ((width << 1) & 0xFFFEu) >> 5;  // 32-byte units of a YUYV line
    Memory::Write32(gx + 0x238u, (0x4Du << 24) | (stride & 0x3FFu));
}
