// RBWE01 busy-waits that rely on preemption.
//
// The runtime switches guest threads only inside OS calls, so a guest loop that
// spins on a flag another thread sets never lets that thread run. On the console
// the retrace interrupt wakes the other (higher-priority) thread and the
// scheduler preempts the spinner. These natives replace such loops with the
// same logic plus "service due interrupts, then yield" in the spin.
#include "hle_stubs.h"

#include <chrono>
#include <cstdint>
#include <thread>
#include "abi_bridge.h"
#include "fiber_manager.h"
#include "guest_interrupt_context.h"
#include "memory.h"

namespace {

constexpr uint32_t kOSYieldThread = 0x802C87C8u;

// One spin iteration's worth of what the hardware would do meanwhile.
void ServiceInterruptsAndYield(CpuContext* ctx) {
    if (Fiber::GuestFiberManager::IsInitialized()) {
        VI_HLE_ProcessRetracesDeferred(8);
        OS_HLE_ProcessAlarmsDeferred(8);
    } else {
        GuestInterruptCallbackContext interrupt;
        VI_HLE_PollRetrace(interrupt.get());
    }
    InvokeIndirectCpu(kOSYieldThread, ctx);
}

} // namespace

// 0x802FDABC: stop the loading-screen thread (entry 0x802FD8E8) and wait for it.
// Clears its "keep drawing" (0x806003E4) and "done" (0x806003E5) flags, spins
// until the thread sets "done", then finishes the frame (0x8032E7E0) and tears
// it down (0x802FD97C) as the original does.
extern "C" void LoadingThreadStop_bw2(CpuContext* ctx) {
    constexpr uint32_t kActive = 0x806003E0u;   // -0xD80(r13)
    constexpr uint32_t kKeepDrawing = 0x806003E4u;
    constexpr uint32_t kDone = 0x806003E5u;
    constexpr uint32_t kBusy = 0x806003E8u;
    const uint32_t self = ctx->gpr[3];
    if (Memory::Read32(kActive) != self) {
        return;
    }
    Memory::Write8(kDone, 0);
    Memory::Write8(kKeepDrawing, 0);
    while (Memory::Read8(kDone) == 0) {
        ServiceInterruptsAndYield(ctx);
        if (Memory::Read8(kDone) == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
    InvokeIndirectCpu(0x8032E7E0u, ctx);
    ctx->gpr[3] = self;
    InvokeIndirectCpu(0x802FD97Cu, ctx);
    Memory::Write8(kBusy, 0);
}
PPC_NATIVE_OVERRIDE_VOID(802FDABC, LoadingThreadStop_bw2, (CpuContext* ctx), (ctx));
