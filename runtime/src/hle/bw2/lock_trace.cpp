// Temporary diagnostic for the lock-on defect (task 07): BW2 stores the locked target's
// id in the SDA word at 0x805FE518 (r13 - 11336) through a one-instruction setter at
// 0x801002AC that is only reached through pointers. This native keeps the store and logs
// each call with the caller's LR when BW2_TRACE_LOCK is set.
#include "bw2_hle.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include "abi_bridge.h"
#include "fiber_manager.h"
#include "guest_interrupt_context.h"
#include "hle_stubs.h"
#include "memory.h"

namespace {
constexpr uint32_t kTargetedObjectId = 0x805FE518u;
}

extern "C" void Bw2_SetTargetedObject_801002AC(uint32_t p) {
    const uint32_t value = Memory::Read32(p);
    Memory::Write32(kTargetedObjectId, value);
    static const bool trace = std::getenv("BW2_TRACE_LOCK") != nullptr;
    if (!trace) return;
    static uint64_t calls = 0;
    ++calls;
    if (calls > 50 && (calls % 300) != 0) return;
    uint32_t lr = 0;
    if (const CpuContext* cpu = TryGetCpuContext()) lr = cpu->lr;
    std::fprintf(stderr, "[lock] call #%llu value=0x%08X from=0x%08X LR=0x%08X\n",
                 static_cast<unsigned long long>(calls), value, p, lr);
}
PPC_NATIVE_OVERRIDE_VOID(801002ac, Bw2_SetTargetedObject_801002AC, (uint32_t p), (p));

// Getters on the lock branch of the player-control function (0x800FF848): the candidate
// target the controller already holds (ctrl+132) and a byte flag (ctrl+6645). Pure loads,
// logged with the controller address so BW2_WATCH can follow its fields.
extern "C" uint32_t Bw2_GetLockCandidate_80100258(uint32_t ctrl) {
    const uint32_t value = Memory::Read32(ctrl + 132u);
    static const bool trace = std::getenv("BW2_TRACE_LOCK") != nullptr;
    if (trace) {
        static uint64_t calls = 0;
        ++calls;
        if (calls <= 50 || (calls % 300) == 0) {
            std::fprintf(stderr, "[lock] candidate #%llu ctrl=0x%08X +24=%u +6645=%u candidate=0x%08X\n",
                         static_cast<unsigned long long>(calls), ctrl, Memory::Read8(ctrl + 24u),
                         Memory::Read8(ctrl + 6645u), value);
        }
    }
    return value;
}
PPC_NATIVE_OVERRIDE(80100258, Bw2_GetLockCandidate_80100258, uint32_t, (uint32_t ctrl), (ctrl));
