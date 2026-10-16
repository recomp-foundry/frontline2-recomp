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
        Audio_HLE_PollDeferred();
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

// 0x80346984: poll the reset/power button state (object at [0x806007A8]). The
// game calls it on every iteration of its busy-waits, among them the three in
// the mission loader (0x80012AAC..0x80012B20: wait for two loader flags, then
// for a fade to finish) that spin until another thread or a retrace callback
// changes what they read. Each call now services due interrupts and yields first
// (what preemption would do on the console), then runs the original logic:
//   if (s->enabled[0xC] && !s->handled[0x9]) {
//       if (!s->pending[0xA]) { if (0x802C9F28()) s->pending[0xA] = 1; }
//       else 0x80346A40(s, 0);
//       if (s->flag[0xB]) 0x80346A40(s, 0);
//   }
extern "C" void ResetButtonPoll_bw2(CpuContext* ctx) {
    const uint32_t self = ctx->gpr[3];
    ServiceInterruptsAndYield(ctx);
    if (Memory::Read8(self + 0xC) == 0 || Memory::Read8(self + 0x9) != 0) {
        return;
    }
    if (Memory::Read8(self + 0xA) == 0) {
        InvokeIndirectCpu(0x802C9F28u, ctx);
        if (ctx->gpr[3] != 0) {
            Memory::Write8(self + 0xA, 1);
        }
    } else {
        ctx->gpr[3] = self;
        ctx->gpr[4] = 0;
        InvokeIndirectCpu(0x80346A40u, ctx);
    }
    if (Memory::Read8(self + 0xB) != 0) {
        ctx->gpr[3] = self;
        ctx->gpr[4] = 0;
        InvokeIndirectCpu(0x80346A40u, ctx);
    }
}
PPC_NATIVE_OVERRIDE_VOID(80346984, ResetButtonPoll_bw2, (CpuContext* ctx), (ctx));
