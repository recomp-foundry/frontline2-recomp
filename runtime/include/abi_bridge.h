#pragma once
#include "memory.h"
#include "ppc_runtime.h"
#include "system_bridge.h"
#include "game_graphics_options.h"
#include "runtime_log.h"

#include <array>
#include <chrono>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <tuple>
#include <type_traits>

inline void InvokeIndirectCpu(uint32_t target, CpuContext* ctx);

// Some game-facing runtime options alter arguments at well-defined ABI
// boundaries. Keep this independent of the dispatch mechanism: generated
// static calls deliberately bypass InvokeDirectCpu for performance.
// (used for the path mask filtering in ScnRenderer::createPath: depth of
// field is always removed, bloom when the user disabled it)
inline bool Bw2IsTracedIndirectTarget(uint32_t target);
inline void Bw2TraceStaticCall(uint32_t target, const CpuContext* ctx);
inline void Bw2TraceStaticReturn(uint32_t target, const CpuContext* ctx);

// BW2_WATCH_WRITER=NAME=ADDR[:OFF...][,NAME=ADDR[:OFF...]...] (hex address, decimal offsets):
// watches the three 32-bit words (a vector x, y, z) at the address each chain resolves to
// (each ':' reads the word at the current address and adds the offset), and at every call
// boundary (entry of any call, return of static, direct and indirect calls) logs the call that
// first sees them changed, so the code writing a guest field can be found without a hardware
// watchpoint. A change seen on a return was made by that callee; one seen on an entry, by the
// caller since its last call boundary. NAME (optional) tags the lines.
struct Bw2WriterWatch {
    std::string name;
    uint32_t base = 0;
    std::vector<uint32_t> offsets;
    uint32_t addr = 0;
    uint32_t last[3] = {};
    bool seen = false;
};

inline std::vector<Bw2WriterWatch>& Bw2GetWriterWatches() {
    static std::vector<Bw2WriterWatch> watches = [] {
        std::vector<Bw2WriterWatch> out;
        const char* v = std::getenv("BW2_WATCH_WRITER");
        if (v == nullptr) return out;
        for (const char* p = v; *p != 0;) {
            Bw2WriterWatch w;
            const char* eq = std::strchr(p, '=');
            const char* comma = std::strchr(p, ',');
            if (eq != nullptr && (comma == nullptr || eq < comma)) {
                w.name.assign(p, static_cast<size_t>(eq - p));
                p = eq + 1;
            }
            char* end = nullptr;
            w.base = static_cast<uint32_t>(std::strtoul(p, &end, 16));
            while (end != nullptr && *end == ':') {
                const char* q = end + 1;
                w.offsets.push_back(static_cast<uint32_t>(std::strtoul(q, &end, 10)));
                if (end == q) break;
            }
            if (w.base != 0) out.push_back(w);
            if (end == nullptr || *end != ',') break;
            p = end + 1;
        }
        return out;
    }();
    return watches;
}

inline void Bw2CheckWriterWatch(const char* kind, uint32_t target, const CpuContext* cpu) {
    std::vector<Bw2WriterWatch>& watches = Bw2GetWriterWatches();
    if (watches.empty() || cpu == nullptr) return;
    static const auto start = std::chrono::steady_clock::now();
    for (Bw2WriterWatch& w : watches) {
        uint32_t addr = w.base;
        bool ok = true;
        for (uint32_t off : w.offsets) {
            if (!Memory::Contains(addr, 4)) { ok = false; break; }
            addr = Memory::Read32(addr) + off;
        }
        if (!ok || !Memory::Contains(addr, 12)) continue;
        const uint32_t now[3] = {Memory::Read32(addr), Memory::Read32(addr + 4), Memory::Read32(addr + 8)};
        if (addr != w.addr) {
            w.addr = addr;
            w.seen = false;
        }
        if (w.seen && now[0] == w.last[0] && now[1] == w.last[1] && now[2] == w.last[2]) continue;
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        float before[3];
        float after[3];
        std::memcpy(before, w.last, sizeof(before));
        std::memcpy(after, now, sizeof(after));
        if (w.seen) {
            std::fprintf(stderr,
                         "[poswrite] %s t=%.3f %s 0x%08X LR=0x%08X r3=0x%08X (%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f) d=(%.4f, %.4f, %.4f)\n",
                         w.name.c_str(), t, kind, target, cpu->lr, cpu->gpr[3], before[0], before[1], before[2],
                         after[0], after[1], after[2], after[0] - before[0], after[1] - before[1],
                         after[2] - before[2]);
        } else {
            std::fprintf(stderr, "[poswrite] %s t=%.3f watching 0x%08X (%.3f, %.3f, %.3f)\n", w.name.c_str(), t,
                         addr, after[0], after[1], after[2]);
        }
        std::memcpy(w.last, now, sizeof(now));
        w.seen = true;
    }
}

// Emitted by the translator after every static translated call (MKW_STATIC_TRANSLATED_CALL):
// the exit-side counterpart of ApplyRuntimeCallOptions, used only by the call trace.
inline void AfterRuntimeStaticCall(uint32_t target, CpuContext* ctx) {
    if (ctx != nullptr && Bw2IsTracedIndirectTarget(target)) Bw2TraceStaticReturn(target, ctx);
    Bw2CheckWriterWatch("ret", target, ctx);
}

inline void ApplyRuntimeCallOptions(uint32_t target, CpuContext* ctx) {
    Bw2CheckWriterWatch("call", target, ctx);
    // BW2_TRACE_INDIRECT also covers the static calls the translated code makes through
    // this hook (entry only: arguments, no return value).
    if (ctx != nullptr && Bw2IsTracedIndirectTarget(target)) Bw2TraceStaticCall(target, ctx);
    (void)target;
    (void)ctx;
    if (false) { // BW2: MKW EGG::ScnRenderer::createPath option, no BW2 equivalent
        // ScnRenderer::createPath receives the post-processing path mask in r4.
        ctx->gpr[4] = RuntimeGameGraphicsOptions::FilterScnRendererPathMask(ctx->gpr[4]);
    }
}

// Persistent per-thread CPU context used across translated function calls.
CpuContext& GetPersistentCpuContext();
void InitializePersistentCpuContext();

enum class FunctionKind : uint8_t {
    BaseTranslated = 0,
    ModTranslated = 1,
    Native = 2,
};

inline constexpr uint32_t kPpcAllNonvolatileFprMask = 0xffffc000u;
inline constexpr uint32_t kBaseTranslatedFunctionPriority = 0;
inline constexpr uint32_t kModTranslatedFunctionPriorityBase = 100;
inline constexpr uint32_t kNativeFunctionPriority = 10000;

#include "isa/ppc_isa_cr.h"

struct TranslatedFunctionInfo {
    uint32_t address = 0;
    const char* name = "";
    uint64_t moduleId = 0;
    uint32_t priority = 0;
    uint32_t nonvolatileFprWriteMask = kPpcAllNonvolatileFprMask;
    void* entryPoint = nullptr; // Raw typed function pointer for indirect-call resolution
    void (*rawCpuInvoker)(CpuContext*) = nullptr;
    bool mustRemainDynamicallyDispatchable = true;
    FunctionKind kind = FunctionKind::BaseTranslated;
};

struct RawDispatchRecord {
    uint32_t address = 0;
    void (*entry)(CpuContext*) = nullptr;
    uint32_t nonvolatileFprWriteMask = kPpcAllNonvolatileFprMask;
    bool preserveNonvolatileGprs = false;
};

// Translator-owned indirect dispatch is split by the high address byte and
// then by 4 KiB guest pages. Each populated segment stores only the contiguous
// page range that contains verified function entry points. The final search is
// bounded to at most the 1024 aligned instruction addresses in one guest page.
struct StaticIndirectDispatchPage {
    uint32_t firstEntry = 0;
    uint16_t entryCount = 0;
    // Explicit tail padding to an 8-byte stride. The emitter writes it as a
    // literal third initializer, so it is part of the generated-data contract.
    uint16_t reserved = 0;
};

struct StaticIndirectDispatchSegment {
    const StaticIndirectDispatchPage* pages = nullptr;
    uint16_t firstPage = 0;
    uint16_t pageCount = 0;
};

struct StaticIndirectDispatchTable {
    const char* profileName = nullptr;
    const StaticIndirectDispatchSegment* segments = nullptr; // Exactly 256 high-byte segments.
    const RawDispatchRecord* entries = nullptr;
    size_t entryCount = 0;
};

inline const RawDispatchRecord* FindStaticIndirectDispatchEntry(
    const StaticIndirectDispatchTable* table,
    uint32_t address) noexcept {
    if (!table || !table->segments || !table->entries || table->entryCount == 0) {
        return nullptr;
    }

    const auto& segment = table->segments[address >> 24];
    const uint32_t pageNumber = (address >> 12) & 0x0FFFu;
    if (!segment.pages || pageNumber < segment.firstPage) {
        return nullptr;
    }
    const uint32_t relativePage = pageNumber - segment.firstPage;
    if (relativePage >= segment.pageCount) {
        return nullptr;
    }

    const auto& page = segment.pages[relativePage];
    const size_t first = page.firstEntry;
    const size_t count = page.entryCount;
    if (first > table->entryCount || count > table->entryCount - first) {
        return nullptr;
    }

    size_t lower = first;
    size_t upper = first + count;
    while (lower < upper) {
        const size_t middle = lower + (upper - lower) / 2;
        const uint32_t candidate = table->entries[middle].address;
        if (candidate < address) {
            lower = middle + 1;
        } else {
            upper = middle;
        }
    }
    return lower < first + count && table->entries[lower].address == address
        ? &table->entries[lower]
        : nullptr;
}

void RegisterStaticIndirectDispatchTable(const StaticIndirectDispatchTable* table);
inline std::atomic<const StaticIndirectDispatchTable*> g_publishedStaticIndirectDispatchTable{nullptr};

class StaticIndirectDispatchTableRegistrar {
public:
    explicit StaticIndirectDispatchTableRegistrar(const StaticIndirectDispatchTable* table) {
        RegisterStaticIndirectDispatchTable(table);
    }
};

inline unsigned PpcLowestSetBitIndex(uint32_t value) noexcept {
    return static_cast<unsigned>(__builtin_ctz(value));
}

class PpcNonvolatileFprGuard {
public:
    explicit PpcNonvolatileFprGuard(CpuContext* cpu, uint32_t mask = kPpcAllNonvolatileFprMask) noexcept
        : cpu_(cpu), mask_(mask & kPpcAllNonvolatileFprMask) {
        if (!cpu_ || mask_ == 0) {
            return;
        }
        // Real masks are a single contiguous register run - f14..f31 for the
        // conservative default and f23..f31 or similar for a measured writer -
        // so resolve the run once and copy it as one straight-line block instead
        // of testing 18 mask bits. Any other mask keeps the per-bit loop, which
        // saves and restores exactly the same registers.
        const uint32_t lowest = mask_ & (0u - mask_);
        const uint32_t aboveRun = mask_ + lowest;
        if ((aboveRun & (aboveRun - 1u)) == 0u) {
            const unsigned first = PpcLowestSetBitIndex(mask_);
            const unsigned end = aboveRun == 0u ? 32u : PpcLowestSetBitIndex(aboveRun);
            firstSlot_ = static_cast<uint8_t>(first - 14u);
            slotCount_ = static_cast<uint8_t>(end - first);
            const size_t count = slotCount_;
            for (size_t i = 0; i < count; ++i) {
                saved_[firstSlot_ + i] = cpu_->fpr[first + i];
            }
            return;
        }
        for (size_t i = 0; i < saved_.size(); ++i) {
            if ((mask_ & (1u << (14 + i))) == 0) {
                continue;
            }
            saved_[i] = cpu_->fpr[14 + i];
        }
    }

    ~PpcNonvolatileFprGuard() noexcept {
        if (!cpu_ || mask_ == 0) {
            return;
        }
        if (slotCount_ != 0) {
            const unsigned first = 14u + firstSlot_;
            const size_t count = slotCount_;
            for (size_t i = 0; i < count; ++i) {
                cpu_->fpr[first + i] = saved_[firstSlot_ + i];
            }
            return;
        }
        for (size_t i = 0; i < saved_.size(); ++i) {
            if ((mask_ & (1u << (14 + i))) == 0) {
                continue;
            }
            cpu_->fpr[14 + i] = saved_[i];
        }
    }

    PpcNonvolatileFprGuard(const PpcNonvolatileFprGuard&) = delete;
    PpcNonvolatileFprGuard& operator=(const PpcNonvolatileFprGuard&) = delete;

private:
    CpuContext* cpu_ = nullptr;
    uint32_t mask_ = 0;
    // Contiguous-run description of mask_. slotCount_ == 0 means the mask is not
    // a single run, so the destructor mirrors the constructor's per-bit loop.
    uint8_t firstSlot_ = 0;
    uint8_t slotCount_ = 0;
    // Every element selected by mask_ is written before it is read. Leaving the
    // remaining slots uninitialized avoids zero-filling all 18 values around a
    // call that may preserve only one or two architectural FPRs.
    std::array<PPC_FPR, 18> saved_;
};

class PpcNonvolatileGprGuard {
public:
    explicit PpcNonvolatileGprGuard(CpuContext* cpu, bool enabled = true) noexcept
        : cpu_(enabled ? cpu : nullptr) {
        if (!cpu_) {
            return;
        }
        for (size_t i = 0; i < saved_.size(); ++i) {
            saved_[i] = cpu_->gpr[14 + i];
        }
    }

    ~PpcNonvolatileGprGuard() noexcept {
        if (!cpu_) {
            return;
        }
        for (size_t i = 0; i < saved_.size(); ++i) {
            cpu_->gpr[14 + i] = saved_[i];
        }
    }

    PpcNonvolatileGprGuard(const PpcNonvolatileGprGuard&) = delete;
    PpcNonvolatileGprGuard& operator=(const PpcNonvolatileGprGuard&) = delete;

private:
    CpuContext* cpu_ = nullptr;
    // Left uninitialized: enabled writes all 18 slots before reading them, disabled reads none,
    // so skipping the zero-fill saves 72 bytes of stack work on the common disabled hot path.
    std::array<uint32_t, 18> saved_;
};

// Guest address of OSLoadContext, which models rfi-style context restoration
// and intentionally replaces the full guest register file instead of returning
// like a normal ABI call. It can never be guarded.
inline constexpr uint32_t kOSLoadContextAddress = 0x802C18A4u;

inline bool ShouldPreserveNonvolatileGprsForRawCpuCall(const TranslatedFunctionInfo* info) noexcept {
    return info->kind == FunctionKind::Native && info->address != kOSLoadContextAddress;
}

inline uint32_t NonvolatileFprGuardMaskFor(const TranslatedFunctionInfo* info) noexcept {
    if (!info || info->nonvolatileFprWriteMask == 0) {
        return 0;
    }
    return info->nonvolatileFprWriteMask & kPpcAllNonvolatileFprMask;
}

template <uint32_t Target>
struct KnownTranslatedCpuCall {
    static constexpr bool kAvailable = false;
    static constexpr uint32_t kNonvolatileFprWriteMask = kPpcAllNonvolatileFprMask;
    static constexpr bool kMustRemainDynamicallyDispatchable = true;
    static constexpr void (*Entry)(CpuContext*) = nullptr;
};

// Translator-emitted trait specializations, one macro shared by every generated shard.
// `addr` is the 8-digit hex entry point, `winner` the resolved symbol (mods/Retro Rewind can
// publish their own name). A specialization only exists for a single statically bound winner,
// so overridable/dynamically-dispatchable are hardcoded here rather than passed in.
#define MKW_TRANSLATED_TRAIT(addr, winner, nonvolatile_fpr_write_mask)                    \
    extern "C" void winner(CpuContext* ctx);                                              \
    template <>                                                                           \
    struct KnownTranslatedCpuCall<0x##addr##u> {                                          \
        static constexpr bool kAvailable = true;                                          \
        static constexpr uint32_t kNonvolatileFprWriteMask = nonvolatile_fpr_write_mask;  \
        static constexpr bool kMustRemainDynamicallyDispatchable = false;                 \
        static constexpr void (*Entry)(CpuContext*) = &winner;                            \
    }

template <uint32_t Target>
struct KnownNativeCpuCall {
    static constexpr bool kAvailable = false;
    static constexpr uint32_t kNonvolatileFprWriteMask = kPpcAllNonvolatileFprMask;
    static constexpr void (*Entry)(CpuContext*) = nullptr;
};

template <uint32_t Target>
struct KnownTypedNativeCpuCall {
    static constexpr bool kAvailable = false;
};

#include "native_cpu_calls.inc"

// Direct-mapped thread-local memo in front of the sorted registry lookup every bctrl performs;
// a miss just re-runs the sorted lookup. 512 entries (12 KiB) covers the per-frame indirect
// working set while staying L1/L2 resident, unlike 4096 which would thrash L2. Must stay a
// power of two, the index masks with (size - 1).
inline constexpr size_t kIndirectDispatchCacheEntries = 512;

// Namespace-scope `inline thread_local`, not function-local `static thread_local`, to avoid a
// thread-static init epoch check on every bctrl (same as g_currentCpuContext in ppc_runtime.h).
struct IndirectResolvedDispatchMemoEntry {
    bool valid;
    uint32_t address;
    const TranslatedFunctionInfo* info;
};
inline thread_local IndirectResolvedDispatchMemoEntry
    g_indirectResolvedDispatchMemo[kIndirectDispatchCacheEntries]{};

struct IndirectRawDispatchMemoEntry {
    bool valid;
    uint32_t address;
    const RawDispatchRecord* record;
};
inline thread_local IndirectRawDispatchMemoEntry
    g_indirectRawDispatchMemo[kIndirectDispatchCacheEntries]{};

class TranslatedFunctionRegistry {
public:
    static void Register(TranslatedFunctionInfo info);
    static void Finalize();
    static inline bool IsLookupPublished() noexcept {
        return lookupPublished_.load(std::memory_order_acquire);
    }
    static std::optional<TranslatedFunctionInfo> FindNearestByAddress(uint32_t address);
    static inline const TranslatedFunctionInfo* FindByAddressPtr(uint32_t address) {
        if (!lookupPublished_.load(std::memory_order_acquire)) {
            return FindByAddressPtrSlow(address);
        }

        auto& cached = g_indirectResolvedDispatchMemo[
            ((address >> 2) * 2654435761u) & (kIndirectDispatchCacheEntries - 1)];
        if (cached.valid && cached.address == address) {
            return cached.info;
        }

        const auto* info = FindByAddressPtrSlow(address);
        cached = {true, address, info};
        return info;
    }

    // Hot wrapper: only the direct-mapped memo probe stays inline. Inlining the
    // generated-table binary search alongside it pushed the whole function past
    // the inliner's budget and out-of-lined the probe itself, which is the part
    // every bctrl executes.
    static MKW_PPC_FORCE_INLINE const RawDispatchRecord* FindRawByAddressPtr(uint32_t address) {
        if (!lookupPublished_.load(std::memory_order_acquire)) {
            return nullptr;
        }

        auto& cached = g_indirectRawDispatchMemo[
            ((address >> 2) * 2654435761u) & (kIndirectDispatchCacheEntries - 1)];
        if (cached.valid && cached.address == address) {
            return cached.record;
        }

        return FindRawByAddressPtrMiss(address, cached);
    }
    // Look up a registered function by host (native) address.
    // This enables stack trace symbolication for translated functions.
    static std::optional<TranslatedFunctionInfo> FindByHostAddress(uintptr_t hostAddr);

private:
    // Memo miss: resolve against the header-inline generated table first; only dynamically
    // registered records or a not-yet-published table fall through to the slow path.
    static MKW_PPC_NO_INLINE MKW_PPC_COLD const RawDispatchRecord* FindRawByAddressPtrMiss(
        uint32_t address, IndirectRawDispatchMemoEntry& cached) {
        const RawDispatchRecord* record = nullptr;
        if (const auto* table =
                g_publishedStaticIndirectDispatchTable.load(std::memory_order_acquire)) {
            record = FindStaticIndirectDispatchEntry(table, address);
        }
        if (record == nullptr) {
            record = FindRawByAddressPtrSlow(address);
        }
        cached = {true, address, record};
        return record;
    }

    static const TranslatedFunctionInfo* FindByAddressPtrSlow(uint32_t address);
    static const RawDispatchRecord* FindRawByAddressPtrSlow(uint32_t address);

    // Finalize publishes immutable registry, generated-table, and dynamic raw
    // dispatch storage. Header-inlined cache hits are enabled only after that
    // release publication, so a pre-finalization miss can never poison them.
    inline static std::atomic_bool lookupPublished_{false};
};

// Cold, profile-owned registration data is emitted in compact table shards.
// Keeping it out of generated function bodies avoids one static constructor
// per translated function.
struct BulkTranslatedFunctionRecord {
    uint32_t address;
    const char* name;
    void (*entry)(CpuContext*);
    FunctionKind kind;
    bool preservesNonvolatileFprs;
    uint32_t nonvolatileFprWriteMask;
    uint32_t priority;
    uint64_t moduleId;
    bool mustRemainDynamicallyDispatchable;
};

void RegisterBulkTranslatedFunctions(const BulkTranslatedFunctionRecord* records, size_t count);

class BulkTranslatedFunctionRegistrar {
public:
    BulkTranslatedFunctionRegistrar(const BulkTranslatedFunctionRecord* records, size_t count) {
        RegisterBulkTranslatedFunctions(records, count);
    }
};

MKW_PPC_FORCE_INLINE bool TryDispatchResolvedCpuTarget(const TranslatedFunctionInfo* info, CpuContext* cpu) {
    if (!info || !info->rawCpuInvoker) {
        return false;
    }

    RecompMod::ScopedTranslatedExecutionAddress translatedExecution(info->address);
    PpcNonvolatileFprGuard fprGuard(cpu, NonvolatileFprGuardMaskFor(info));
    PpcNonvolatileGprGuard gprGuard(cpu, ShouldPreserveNonvolatileGprsForRawCpuCall(info));
    if (TryGetCpuContext() != cpu) {
        CpuContextScope scope(cpu);
        info->rawCpuInvoker(cpu);
        return true;
    }
    info->rawCpuInvoker(cpu);
    return true;
}

inline bool TryDispatchRawCpuTarget(const RawDispatchRecord* record, CpuContext* cpu) {
    if (!record || !record->entry) {
        return false;
    }
    RecompMod::ScopedTranslatedExecutionAddress translatedExecution(record->address);
    PpcNonvolatileGprGuard gprGuard(cpu, record->preserveNonvolatileGprs);
    PpcNonvolatileFprGuard fprGuard(cpu, record->nonvolatileFprWriteMask);
    if (TryGetCpuContext() != cpu) {
        CpuContextScope scope(cpu);
        record->entry(cpu);
    } else {
        record->entry(cpu);
    }
    return true;
}

template <uint32_t Target>
inline void DispatchKnownTranslatedCpuTargetStatic(CpuContext* cpu) {
    const auto invokeKnownTranslated = [&]() {
        KnownTranslatedCpuCall<Target>::Entry(cpu);
    };

    // A statically resolved, non-overridable translated call already running
    // in this CpuContext needs no registry, context, or diagnostic target
    // transition. This is the normal generated-to-generated hot path.
    if (TryGetCpuContext() == cpu) {
        if constexpr (KnownTranslatedCpuCall<Target>::kNonvolatileFprWriteMask == 0) {
            invokeKnownTranslated();
            return;
        } else {
            PpcNonvolatileFprGuard fprGuard(
                cpu, KnownTranslatedCpuCall<Target>::kNonvolatileFprWriteMask);
            invokeKnownTranslated();
            return;
        }
    }

    CpuContextScope contextScope(cpu);
    RecompMod::ScopedTranslatedExecutionAddress translatedExecution(Target);
    if constexpr (KnownTranslatedCpuCall<Target>::kNonvolatileFprWriteMask == 0) {
        invokeKnownTranslated();
        return;
    } else {
        PpcNonvolatileFprGuard fprGuard(cpu, KnownTranslatedCpuCall<Target>::kNonvolatileFprWriteMask);
        invokeKnownTranslated();
    }
}

[[noreturn]] inline void ReportMissingCpuTarget(uint32_t target, CpuContext* cpu) {
    // Centralized crash path: stderr diagnostics, heuristics, crash artifacts
    // in the run's log folder, popup, exit.
    RuntimeCrash::FatalMissingGuestTarget(target, cpu);
}

template <uint32_t Target>
inline const TranslatedFunctionInfo* ResolveDirectCpuTargetInfo() {
    return TranslatedFunctionRegistry::FindByAddressPtr(Target);
}

template <uint32_t Target>
inline const TranslatedFunctionInfo* ResolveDirectCpuTargetInfoCached() {
    // Only latch a result once the registry is published (immutable for the process lifetime);
    // a pre-finalization miss or provisional winner must never be cached. 0 = not yet resolved,
    // 1 = negative-cache sentinel, avoiding a second ready flag on the hot path.
    static std::atomic<uintptr_t> cachedValue{0};
    constexpr uintptr_t kNegativeCache = 1;
    const uintptr_t cached = cachedValue.load(std::memory_order_acquire);
    if (cached != 0) {
        return cached == kNegativeCache
            ? nullptr
            : reinterpret_cast<const TranslatedFunctionInfo*>(cached);
    }
    if (!TranslatedFunctionRegistry::IsLookupPublished()) {
        return ResolveDirectCpuTargetInfo<Target>();
    }
    const auto* info = ResolveDirectCpuTargetInfo<Target>();
    cachedValue.store(info != nullptr
                          ? reinterpret_cast<uintptr_t>(info)
                          : kNegativeCache,
                      std::memory_order_release);
    return info;
}

// Caches the "is the published winner still base translation?" verdict for state-free fast
// paths guarding tens of thousands of call sites. Same latching rule as above: 0 = unresolved,
// 1 = negative, 2 = positive, and a verdict computed pre-publication is never stored.
template <uint32_t Target>
inline bool IsBaseTranslatedCpuTargetActive() {
    static std::atomic<uint8_t> cachedVerdict{0};
    constexpr uint8_t kNegativeVerdict = 1;
    constexpr uint8_t kPositiveVerdict = 2;
    const uint8_t cached = cachedVerdict.load(std::memory_order_acquire);
    if (cached != 0) {
        return cached == kPositiveVerdict;
    }
    const bool publishable = TranslatedFunctionRegistry::IsLookupPublished();
    const auto* info = ResolveDirectCpuTargetInfo<Target>();
    const bool active = info != nullptr && info->kind == FunctionKind::BaseTranslated;
    if (publishable) {
        cachedVerdict.store(active ? kPositiveVerdict : kNegativeVerdict,
                            std::memory_order_release);
    }
    return active;
}

// State of one traced call (BW2_TRACE_INDIRECT); filled by Bw2TraceCallBegin below.
struct Bw2TraceCallState {
    bool traced = false;
    uint32_t target = 0;
    uint64_t call = 0;
    uint32_t r3 = 0, r4 = 0, r5 = 0;
    uint32_t lr = 0;
    double f1 = 0.0;
};
inline Bw2TraceCallState Bw2TraceCallBegin(uint32_t target, const CpuContext* cpu);
inline void Bw2TraceCallEnd(const Bw2TraceCallState& st, const CpuContext* cpu);
inline bool Bw2IsTracedIndirectTarget(uint32_t target);

template <uint32_t Target>
inline void InvokeDirectCpuImpl(CpuContext* ctx) {
    static_assert(Target != 0, "InvokeDirectCpu cannot target address 0");
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    ApplyRuntimeCallOptions(Target, cpu);
    // Return-side writer watch (BW2_WATCH_WRITER) and call trace (BW2_TRACE_INDIRECT) for
    // direct calls, whichever path returns.
    struct WriterWatchOnReturn {
        CpuContext* cpu;
        ~WriterWatchOnReturn() {
            if (Bw2IsTracedIndirectTarget(Target)) Bw2TraceStaticReturn(Target, cpu);
            Bw2CheckWriterWatch("dret", Target, cpu);
        }
    } writerWatchOnReturn{cpu};
    if constexpr (KnownNativeCpuCall<Target>::kAvailable) {
        const auto invokeKnownNative = [&]() {
            PpcNonvolatileGprGuard gprGuard(cpu);
            if (TryGetCpuContext() != cpu) {
                CpuContextScope scope(cpu);
                KnownNativeCpuCall<Target>::Entry(cpu);
                return;
            }
            KnownNativeCpuCall<Target>::Entry(cpu);
        };
        if constexpr (KnownNativeCpuCall<Target>::kNonvolatileFprWriteMask == 0) {
            invokeKnownNative();
        } else {
            PpcNonvolatileFprGuard fprGuard(cpu, KnownNativeCpuCall<Target>::kNonvolatileFprWriteMask);
            invokeKnownNative();
        }
        return;
    }

    if constexpr (KnownTypedNativeCpuCall<Target>::kAvailable) {
        PpcNonvolatileGprGuard gprGuard(cpu);
        if (TryGetCpuContext() != cpu) {
            CpuContextScope scope(cpu);
            KnownTypedNativeCpuCall<Target>::Invoke(cpu);
        } else {
            KnownTypedNativeCpuCall<Target>::Invoke(cpu);
        }
        return;
    }

    if constexpr (KnownTranslatedCpuCall<Target>::kAvailable) {
        DispatchKnownTranslatedCpuTargetStatic<Target>(cpu);
        return;
    }

    const auto* registeredInfo = ResolveDirectCpuTargetInfoCached<Target>();
    if (TryDispatchResolvedCpuTarget(registeredInfo, cpu)) {
        return;
    }

    ReportMissingCpuTarget(Target, cpu);
}

template <uint32_t Target>
inline void InvokeDirectCpu(CpuContext* ctx) {
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    if (!Bw2IsTracedIndirectTarget(Target)) {
        InvokeDirectCpuImpl<Target>(cpu);
        return;
    }
    const Bw2TraceCallState trace = Bw2TraceCallBegin(Target, cpu);
    InvokeDirectCpuImpl<Target>(cpu);
    Bw2TraceCallEnd(trace, cpu);
}

// Indirect jump (bctr without link) - used for switch tables.
inline void InvokeIndirectJump(uint32_t target, CpuContext* ctx) {
    // Use the caller's context, not the persistent global context!
    // This is essential for tail calls via bctr where the 'this' pointer (r3)
    // must be passed correctly to the target function.
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    if (TryDispatchRawCpuTarget(TranslatedFunctionRegistry::FindRawByAddressPtr(target), cpu)) {
        return;
    }

    // Cold fallback for dynamically registered or signature-only targets.
    const auto* info = TranslatedFunctionRegistry::FindByAddressPtr(target);
    if (info) {
        if (TryDispatchResolvedCpuTarget(info, cpu)) {
            return;
        }
    }
    
    // If not registered, this is likely a switch table jump to an intra-function label.
    // The translator should have recognized this pattern and generated proper switch code.
    RT_LOG(RT_TAG_RUNTIME) << "InvokeIndirectJump: target 0x"
              << std::hex << target << std::dec
              << " is not a registered function.\n"
              << "This is likely a switch statement jump table that the translator\n"
              << "failed to recognize. The function containing this bctr needs\n"
              << "proper switch pattern recognition." << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << "Indirect jump context: pc=0x" << std::hex << cpu->pc
              << " lr=0x" << cpu->lr << " ctr=0x" << cpu->ctr
              << " r1=0x" << cpu->gpr[1] << " r3=0x" << cpu->gpr[3]
              << " r29=0x" << cpu->gpr[29] << " r30=0x" << cpu->gpr[30]
              << " r31=0x" << cpu->gpr[31] << std::dec << std::endl;
    DumpHostStackTraceForRuntimeHelper();
    std::fflush(stderr);
    std::ostringstream message;
    message << "The game stopped because an indirect jump targeted guest address 0x"
            << std::hex << target
            << ", but that address is not a registered translated function.\n\n"
            << "The translator may have missed a switch/jump-table target.";
    // Same artifact set as RuntimeCrash::FatalMissingGuestTarget, which is this
    // path's sibling for indirect *calls*: MarkFatalErrorReported below stops the
    // atexit reporter, so the crash log has to be written here.
    RuntimeCrash::WriteCrashArtifacts("missing_jump_target", message.str(), &target);
    SetRuntimeExitCode(EXIT_FAILURE);
    ShowRuntimeFatalPopup("Missing indirect jump target", message.str());
    MarkFatalErrorReported();
    std::exit(EXIT_FAILURE);
}

// BW2_TRACE_INDIRECT=<hex guest address>: log every indirect call to that target
// (r3/r4 on entry, r3 on return), the first 40 and then one in 500. Diagnostic for
// callbacks that cannot be wrapped natively (an override removes their translation).
// Several targets separated by commas are accepted. BW2_TRACE_INDIRECT_DEREF="a:b:c,..."
// adds, per traced call, the words reached by following each offset chain from r4
// (word at r4+a, then +b from it, then +c): vtable slots of the object a query returns.
// BW2_TRACE_INDIRECT_FROM_FRAME=<n>: trace only once the GX frame counter reaches n, so a
// full (EVERY=1) trace can be limited to the part of a scripted run that matters.
extern "C" int g_gxFrameCount;
inline bool Bw2TraceFrameWindowOpen() {
    static const long fromFrame = [] {
        const char* v = std::getenv("BW2_TRACE_INDIRECT_FROM_FRAME");
        return v != nullptr ? std::strtol(v, nullptr, 10) : 0L;
    }();
    return fromFrame <= 0 || g_gxFrameCount >= fromFrame;
}

inline bool Bw2IsTracedIndirectTarget(uint32_t target) {
    if (!Bw2TraceFrameWindowOpen()) return false;
    static const std::vector<uint32_t> targets = [] {
        std::vector<uint32_t> out;
        if (const char* v = std::getenv("BW2_TRACE_INDIRECT")) {
            for (const char* p = v; *p != 0;) {
                char* end = nullptr;
                const unsigned long value = std::strtoul(p, &end, 16);
                if (end == p) break;
                out.push_back(static_cast<uint32_t>(value));
                p = (*end == ',') ? end + 1 : end;
            }
        }
        return out;
    }();
    for (uint32_t t : targets) {
        if (t == target) return true;
    }
    return false;
}

// A chain starting with "r3" follows the offsets from r3 instead of r4; "ret" from the r3
// the call returned.
inline std::string Bw2TracedIndirectDerefs(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t ret) {
    static const std::vector<std::vector<uint32_t>> chains = [] {
        std::vector<std::vector<uint32_t>> out;
        if (const char* v = std::getenv("BW2_TRACE_INDIRECT_DEREF")) {
            std::vector<uint32_t> chain;
            for (const char* p = v; ; ++p) {
                if (*p == 'r' && p[1] == '3') { chain.push_back(0xFFFFFFFFu); p += 1; continue; }
                if (*p == 'r' && p[1] == 'e' && p[2] == 't') { chain.push_back(0xFFFFFFFEu); p += 2; continue; }
                if (*p == 'r' && p[1] == '5') { chain.push_back(0xFFFFFFFDu); p += 1; continue; }
                if (*p == ':' || *p == ',' || *p == 0) {
                    if (*p == ',' || *p == 0) {
                        if (!chain.empty()) out.push_back(chain);
                        chain.clear();
                    }
                    if (*p == 0) break;
                    continue;
                }
                char* end = nullptr;
                chain.push_back(static_cast<uint32_t>(std::strtoul(p, &end, 10)));
                if (end == p) break;
                p = end - 1;
            }
        }
        return out;
    }();
    std::string text;
    for (const auto& chain : chains) {
        uint32_t addr = r4;
        bool ok = true;
        for (uint32_t off : chain) {
            if (off == 0xFFFFFFFFu) { addr = r3; continue; }
            if (off == 0xFFFFFFFEu) { addr = ret; continue; }
            if (off == 0xFFFFFFFDu) { addr = r5; continue; }
            const uint32_t at = addr + off;
            if (!Memory::Contains(at, 4u)) { ok = false; break; }
            addr = Memory::Read32(at);
        }
        char buf[48];
        std::snprintf(buf, sizeof(buf), " deref=%s0x%08X", ok ? "" : "?", addr);
        text += buf;
    }
    return text;
}


// Call trace shared by InvokeIndirectCpu and InvokeDirectCpu (see BW2_TRACE_INDIRECT).
// BW2_TRACE_INDIRECT_R3=<hex>: only calls whose r3 (this) equals that value are logged.
inline Bw2TraceCallState Bw2TraceCallBegin(uint32_t target, const CpuContext* cpu) {
    Bw2TraceCallState st;
    // BW2_TRACE_INDIRECT_LR=ADDR[,ADDR...]: also trace the indirect calls made from these
    // return addresses, whatever their target (finds the target of a virtual call site).
    static const std::vector<uint32_t> fromLr = [] {
        std::vector<uint32_t> out;
        if (const char* v = std::getenv("BW2_TRACE_INDIRECT_LR")) {
            for (const char* p = v; *p != 0;) {
                char* end = nullptr;
                const unsigned long value = std::strtoul(p, &end, 16);
                if (end == p) break;
                out.push_back(static_cast<uint32_t>(value));
                p = (*end == ',') ? end + 1 : end;
            }
        }
        return out;
    }();
    bool traced = target != 0 && Bw2IsTracedIndirectTarget(target);
    if (!traced && target != 0 && cpu != nullptr) {
        for (uint32_t lr : fromLr) {
            if (cpu->lr == lr) { traced = true; break; }
        }
    }
    if (!traced) return st;
    static const uint32_t onlyR3 = [] {
        const char* v = std::getenv("BW2_TRACE_INDIRECT_R3");
        return v != nullptr ? static_cast<uint32_t>(std::strtoul(v, nullptr, 16)) : 0u;
    }();
    if (onlyR3 != 0 && cpu->gpr[3] != onlyR3) return st;
    static uint64_t calls = 0;
    st.traced = true;
    st.target = target;
    st.call = ++calls;
    st.r3 = cpu->gpr[3];
    st.r4 = cpu->gpr[4];
    st.r5 = cpu->gpr[5];
    st.lr = cpu->lr;
    st.f1 = cpu->fpr[1].d;
    return st;
}

inline void Bw2TraceCallEnd(const Bw2TraceCallState& st, const CpuContext* cpu) {
    if (!st.traced) return;
    static const uint64_t traceFirst = [] {
        const char* v = std::getenv("BW2_TRACE_INDIRECT_FIRST");
        return v != nullptr ? std::strtoull(v, nullptr, 10) : 40ull;
    }();
    static const uint64_t traceEvery = [] {
        const char* v = std::getenv("BW2_TRACE_INDIRECT_EVERY");
        const uint64_t n = v != nullptr ? std::strtoull(v, nullptr, 10) : 500ull;
        return n == 0 ? 1ull : n;
    }();
    if (!(st.call <= traceFirst || (st.call % traceEvery) == 0)) return;
    uint32_t ctxWords[8] = {};
    for (uint32_t i = 0; i < 8; ++i) {
        ctxWords[i] = Memory::Contains(st.r4 + i * 4u, 4u) ? Memory::Read32(st.r4 + i * 4u) : 0u;
    }
    std::fprintf(stderr,
                 "[indirect] 0x%08X #%llu LR=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X f1=%g -> r3=0x%08X f1=%g r4[0..7]=%08X %08X "
                 "%08X %08X %08X %08X %08X %08X%s\n",
                 st.target, static_cast<unsigned long long>(st.call), st.lr, st.r3, st.r4, st.r5, st.f1, cpu->gpr[3],
                 cpu->fpr[1].d, ctxWords[0], ctxWords[1], ctxWords[2], ctxWords[3], ctxWords[4], ctxWords[5],
                 ctxWords[6], ctxWords[7], Bw2TracedIndirectDerefs(st.r3, st.r4, st.r5, cpu->gpr[3]).c_str());
}

inline void Bw2TraceStaticCall(uint32_t target, const CpuContext* cpu) {
    static uint64_t calls = 0;
    static const uint64_t traceEvery = [] {
        const char* v = std::getenv("BW2_TRACE_INDIRECT_EVERY");
        const uint64_t n = v != nullptr ? std::strtoull(v, nullptr, 10) : 500ull;
        return n == 0 ? 1ull : n;
    }();
    static const uint32_t onlyR3 = [] {
        const char* v = std::getenv("BW2_TRACE_INDIRECT_R3");
        return v != nullptr ? static_cast<uint32_t>(std::strtoul(v, nullptr, 16)) : 0u;
    }();
    if (onlyR3 != 0 && cpu->gpr[3] != onlyR3) return;
    ++calls;
    if ((calls % traceEvery) != 0) return;
    std::fprintf(stderr, "[static] 0x%08X #%llu r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X f1=%g%s\n", target,
                 static_cast<unsigned long long>(calls), cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6],
                 cpu->fpr[1].d, Bw2TracedIndirectDerefs(cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], 0u).c_str());
}

// BW2_TRACE_INDIRECT_DEREF chains are evaluated again on return (r3/r4/r5 as left by the callee),
// so an in/out buffer passed in r4 can be read after the call.
inline void Bw2TraceStaticReturn(uint32_t target, const CpuContext* cpu) {
    static uint64_t calls = 0;
    static const uint64_t traceEvery = [] {
        const char* v = std::getenv("BW2_TRACE_INDIRECT_EVERY");
        const uint64_t n = v != nullptr ? std::strtoull(v, nullptr, 10) : 500ull;
        return n == 0 ? 1ull : n;
    }();
    ++calls;
    if ((calls % traceEvery) != 0) return;
    // f0..f7 as the raw 64-bit pattern: a paired-single register holds ps0 in the high
    // 32 bits and ps1 in the low 32 (union PPC_FPR), a scalar one an IEEE double.
    char fprs[8 * 20];
    int n = 0;
    for (int i = 0; i < 8; ++i) {
        uint64_t bits;
        std::memcpy(&bits, &cpu->fpr[i].d, sizeof(bits));
        n += std::snprintf(fprs + n, sizeof(fprs) - static_cast<size_t>(n), " f%d=%016llX", i,
                           static_cast<unsigned long long>(bits));
    }
    std::fprintf(stderr, "[static-ret] 0x%08X #%llu r3=0x%08X r4=0x%08X r5=0x%08X f1=%g%s%s\n", target,
                 static_cast<unsigned long long>(calls), cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->fpr[1].d,
                 fprs, Bw2TracedIndirectDerefs(cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[3]).c_str());
}

inline void InvokeIndirectCpu(uint32_t target, CpuContext* ctx) {
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    if (target == 0) {
        ReportMissingCpuTarget(target, cpu);
    }
    ApplyRuntimeCallOptions(target, cpu);
    Bw2TraceCallState trace = Bw2TraceCallBegin(target, cpu);
    bool dispatched = TryDispatchRawCpuTarget(TranslatedFunctionRegistry::FindRawByAddressPtr(target), cpu);
    if (!dispatched) {
        dispatched = TryDispatchResolvedCpuTarget(TranslatedFunctionRegistry::FindByAddressPtr(target), cpu);
    }
    if (dispatched) Bw2TraceCallEnd(trace, cpu);
    if (dispatched) Bw2CheckWriterWatch("iret", target, cpu);
    if (dispatched) {
        return;
    }
    ReportMissingCpuTarget(target, cpu);
}

template <typename Signature>
class AbiTrampoline;

template <typename Ret, typename... Args>
class AbiTrampoline<Ret(Args...)> {
public:
    using TargetFn = Ret (*)(Args...);

    AbiTrampoline(uint32_t address,
                  const char* name,
                  TargetFn target,
                  FunctionKind kind = FunctionKind::BaseTranslated,
                  bool preservesNonvolatileFprs = false,
                  uint32_t nonvolatileFprWriteMask = kPpcAllNonvolatileFprMask,
                  uint32_t priority = 0,
                  uint64_t moduleId = 0,
                  void (*rawCpuInvoker)(CpuContext*) = nullptr,
                  bool mustRemainDynamicallyDispatchable = true)
    {
        TranslatedFunctionInfo info;
        info.address = address;
        info.name = name ? name : "";
        info.moduleId = moduleId;
        info.priority = priority;
        const bool effectivePreservesNonvolatileFprs =
            preservesNonvolatileFprs || kind == FunctionKind::Native;
        info.nonvolatileFprWriteMask = effectivePreservesNonvolatileFprs ? 0 : (nonvolatileFprWriteMask & kPpcAllNonvolatileFprMask);
        info.entryPoint = reinterpret_cast<void*>(target);
        info.rawCpuInvoker = rawCpuInvoker;
        info.mustRemainDynamicallyDispatchable = mustRemainDynamicallyDispatchable;
        info.kind = kind;
        TranslatedFunctionRegistry::Register(std::move(info));
    }

    static std::tuple<Args...> BuildArgs(CpuContext* cpu) {
        size_t gprIndex = 0;
        size_t fprIndex = 0;
        return std::tuple<Args...>{LoadArgument<Args>(cpu, gprIndex, fprIndex)...};
    }

    template <typename T>
    static std::remove_reference_t<T> LoadArgument(CpuContext* cpu, size_t& gprIndex, size_t& fprIndex) {
        using CleanT = std::remove_reference_t<T>;
        if constexpr (std::is_floating_point_v<CleanT>) {
            if (fprIndex >= 13) {
                throw std::out_of_range("FPR argument overflow");
            }
            return static_cast<CleanT>(cpu->fpr[1 + fprIndex++].d);
        } else {
            if (gprIndex >= 8) {
                size_t stackArgIndex = gprIndex - 8;
                uint32_t sp = cpu->gpr[1];
                uint32_t argAddr = sp + 8 + static_cast<uint32_t>(stackArgIndex * 4);
                gprIndex++;
                try {
                    return static_cast<CleanT>(Memory::Read32(argAddr));
                } catch (const Memory::AccessViolation&) {
                    RT_LOG(RT_TAG_RUNTIME) << "Stack parameter read failed at 0x" << std::hex << argAddr
                              << " (SP=0x" << sp << ", argIndex=" << std::dec << (gprIndex - 1) << ")" << std::endl;
                    throw;
                }
            }
            return static_cast<CleanT>(cpu->gpr[3 + gprIndex++]);
        }
    }

    static void InvokeCpu(TargetFn target, CpuContext* cpu) {
        auto args = BuildArgs(cpu);
        if constexpr (std::is_void_v<Ret>) {
            std::apply(target, args);
        } else {
            auto result = std::apply(target, args);
            if constexpr (std::is_floating_point_v<Ret>) {
                cpu->fpr[1].d = static_cast<double>(result);
            } else {
                cpu->gpr[3] = static_cast<uint32_t>(result);
            }
        }
    }
};

template <auto Target>
struct AbiRawCpuThunk {
    static void Invoke(CpuContext* cpu) {
        if constexpr (std::is_invocable_v<decltype(Target), CpuContext*>) {
            (void)Target(cpu);
        } else {
            AbiTrampoline<std::remove_pointer_t<decltype(Target)>>::InvokeCpu(Target, cpu);
        }
    }
};

template <typename Ret>
class AbiTrampoline<Ret(CpuContext*)> {
public:
    using TargetFn = Ret (*)(CpuContext*);

    AbiTrampoline(uint32_t address,
                  const char* name,
                  TargetFn target,
                  FunctionKind kind = FunctionKind::BaseTranslated,
                  bool preservesNonvolatileFprs = false,
                  uint32_t nonvolatileFprWriteMask = kPpcAllNonvolatileFprMask,
                  uint32_t priority = 0,
                  uint64_t moduleId = 0,
                  void (*rawCpuInvoker)(CpuContext*) = nullptr,
                  bool mustRemainDynamicallyDispatchable = true)
    {
        TranslatedFunctionInfo info;
        info.address = address;
        info.name = name ? name : "";
        info.moduleId = moduleId;
        info.priority = priority;
        // Deliberately *not* mirroring the typed specialization's
        // `|| kind == FunctionKind::Native`. A CpuContext* native receives the
        // whole guest register file and can legitimately write f14-f31 through
        // it, so it stays conservative until the individual address has been
        // measured clean and its registration opts out below.
        info.nonvolatileFprWriteMask = preservesNonvolatileFprs ? 0 : (nonvolatileFprWriteMask & kPpcAllNonvolatileFprMask);
        info.entryPoint = reinterpret_cast<void*>(target);
        info.rawCpuInvoker = rawCpuInvoker ? rawCpuInvoker : reinterpret_cast<void (*)(CpuContext*)>(target);
        info.mustRemainDynamicallyDispatchable = mustRemainDynamicallyDispatchable;
        info.kind = kind;
        TranslatedFunctionRegistry::Register(std::move(info));
    }
};

#define MKW_DETAIL_CAT(a, b) a##b
#define MKW_DETAIL_MAKE_UNIQUE(a, b) MKW_DETAIL_CAT(a, b)

// Three hand-written registration macros. Generated code never registers per function; every
// translated function reaches the registry via the bulk BulkTranslatedFunctionRecord tables
// in the translator's *_registration TUs.
#define REGISTER_TRANSLATED_FUNCTION(address, fn) \
    static AbiTrampoline<decltype(fn)> MKW_DETAIL_MAKE_UNIQUE(_abi_trampoline_, __COUNTER__)(address, #fn, fn, FunctionKind::BaseTranslated, false, kPpcAllNonvolatileFprMask, 0, 0, nullptr, KnownTranslatedCpuCall<address>::kMustRemainDynamicallyDispatchable)

#define REGISTER_NATIVE_FUNCTION(address, fn) \
    static AbiTrampoline<decltype(fn)> MKW_DETAIL_MAKE_UNIQUE(_abi_native_trampoline_, __COUNTER__)(address, #fn, fn, FunctionKind::Native, false, kPpcAllNonvolatileFprMask, 0, 0, &AbiRawCpuThunk<&fn>::Invoke)

#define REGISTER_NATIVE_FUNCTION_AS(address, fn, pretty_name) \
    static AbiTrampoline<decltype(fn)> MKW_DETAIL_MAKE_UNIQUE(_abi_native_trampoline_named_, __COUNTER__)(address, pretty_name, fn, FunctionKind::Native, false, kPpcAllNonvolatileFprMask, 0, 0, &AbiRawCpuThunk<&fn>::Invoke)
