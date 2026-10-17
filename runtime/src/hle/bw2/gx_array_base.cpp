// Raw CP array bases for Aurora.
//
// BW2's renderer changes vertex array bases inside its own display lists and
// FIFO writes (CP registers 0xA0-0xAF, console physical addresses). Aurora only
// knew its own GX_LOAD_AURORA_ARRAYBASE command and ignored those writes, so
// every draw after one read the previous array: no 3D geometry on screen. This
// resolver maps the physical address to the guest RAM it names.
#include <aurora/aurora.h>

#include <cstdint>
#include "bw2_hle.h"
#include "hle/gx/gx_internal.h"
#include "memory.h"

namespace {

// Spans of the arrays the display list being decoded reads (GX__CallDisplayList fills
// this from its scan, see DlArrayRange in hle/gx/gx_dl.cpp). A base the list writes
// itself resolves to the bytes its draws cover instead of the rest of the RAM region,
// which Aurora would otherwise upload whole (StagingCapacityError past 8 MB).
struct ArraySpan {
    uint32_t base = 0;
    uint32_t size = 0;
};
constexpr size_t kMaxArraySpans = 128;
ArraySpan g_arraySpans[kMaxArraySpans];
size_t g_arraySpanCount = 0;

const void* ResolveCpArrayBase(uint32_t cpValue, uint32_t* outSize) {
    const uint32_t address = DecodeCpArrayBaseGuestAddress(cpValue);
    if (address == 0) {
        return nullptr;
    }
    const uint32_t canonical = CanonicalizeGxMainRamAddress(address);
    for (size_t i = 0; i < g_arraySpanCount; ++i) {
        if (g_arraySpans[i].base == canonical && Memory::Contains(address, g_arraySpans[i].size)) {
            *outSize = g_arraySpans[i].size;
            return Memory::GetPointer(address, g_arraySpans[i].size);
        }
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

void Bw2ClearDisplayListArraySpans() { g_arraySpanCount = 0; }

void Bw2AddDisplayListArraySpan(unsigned int guestBase, unsigned int size) {
    const uint32_t canonical = CanonicalizeGxMainRamAddress(guestBase);
    for (size_t i = 0; i < g_arraySpanCount; ++i) {
        if (g_arraySpans[i].base == canonical) {
            if (size > g_arraySpans[i].size) g_arraySpans[i].size = size;
            return;
        }
    }
    if (g_arraySpanCount < kMaxArraySpans) {
        g_arraySpans[g_arraySpanCount++] = ArraySpan{canonical, size};
    }
}
