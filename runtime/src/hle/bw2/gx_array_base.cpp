// Raw CP array bases for Aurora.
//
// BW2's renderer changes vertex array bases inside its own display lists and
// FIFO writes (CP registers 0xA0-0xAF, console physical addresses). Aurora only
// knew its own GX_LOAD_AURORA_ARRAYBASE command and ignored those writes, so
// every draw after one read the previous array: no 3D geometry on screen. This
// resolver maps the physical address to the guest RAM it names.
#include <aurora/aurora.h>

#include <cstdint>
#include "hle/gx/gx_internal.h"
#include "memory.h"

namespace {

const void* ResolveCpArrayBase(uint32_t cpValue, uint32_t* outSize) {
    const uint32_t address = DecodeCpArrayBaseGuestAddress(cpValue);
    if (address == 0) {
        return nullptr;
    }
    // Bytes left in the RAM region that holds it (MEM1 or MEM2, any alias).
    uint32_t end = 0;
    if (address >= Memory::kMem1CachedBase && address < Memory::kMem1CachedBase + Memory::kMem1Size) {
        end = Memory::kMem1CachedBase + static_cast<uint32_t>(Memory::kMem1Size);
    } else if (address >= Memory::kMem2CachedBase && address < Memory::kMem2CachedEnd) {
        end = Memory::kMem2CachedEnd;
    } else if (address < Memory::kMem1Size) {
        end = static_cast<uint32_t>(Memory::kMem1Size);
    } else if (address >= Memory::kMem2PhysicalBase && address < Memory::kMem2PhysicalEnd) {
        end = Memory::kMem2PhysicalEnd;
    } else {
        return nullptr;
    }
    const uint32_t size = end - address;
    if (!Memory::Contains(address, size)) {
        return nullptr;
    }
    *outSize = size;
    return Memory::GetPointer(address, size);
}

struct Register {
    Register() { aurora_set_array_base_resolver(ResolveCpArrayBase); }
} g_register;

} // namespace
