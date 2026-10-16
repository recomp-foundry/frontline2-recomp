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

#include <atomic>
#include <cstdint>
#include "abi_bridge.h"
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

void Bw2DeliverGxBreakpoint(CpuContext* ctx) {
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
