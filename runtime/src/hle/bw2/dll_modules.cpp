// RBWE01 DLL modules (RSO) served natively.
//
// The game's own DLL manager loads files/DLL/FinalRelease/wiizLib.rso and
// HomeButtonMenu.rso.gz at run time (LoadDLL 0x8036D7C4), runs their prolog and
// binds their exports into function-pointer variables (ImportSymbols
// 0x8036DB58, tables 0x804CC298 and 0x804CC2B0). RSO code is not translated, so
// both calls are replaced here: LoadDLL returns a placeholder handle without
// loading, and ImportSymbols binds every imported name to a native function
// registered at a fixed guest address in otherwise unused low memory.
//
// wiizLib is zlib's inflate side; it is served by the host zlib. The HOME
// button menu is not implemented yet: HBMCalc answers HBM_SELECT_HOMEBTN, which
// closes the menu, and the rest are no-ops.
#include "hle_stubs.h"

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "memory.h"

namespace {

constexpr uint32_t kModuleHandle = 0x80001980u;  // zeroed placeholder returned by LoadDLL

// Native entry points (guest addresses only used as registry keys).
constexpr uint32_t kInflateInit2 = 0x80001900u;
constexpr uint32_t kInflateEnd = 0x80001904u;
constexpr uint32_t kInflate = 0x80001908u;
constexpr uint32_t kHbmCreate = 0x80001910u;
constexpr uint32_t kHbmDelete = 0x80001914u;
constexpr uint32_t kHbmInit = 0x80001918u;
constexpr uint32_t kHbmCalc = 0x8000191Cu;
constexpr uint32_t kHbmDraw = 0x80001920u;
constexpr uint32_t kHbmGetSelectBtnNum = 0x80001924u;
constexpr uint32_t kHbmSetAdjustFlag = 0x80001928u;
constexpr uint32_t kHbmCreateDvdSound = 0x8000192Cu;
constexpr uint32_t kHbmUpdateSound = 0x80001930u;

const std::map<std::string, uint32_t>& Exports() {
    static const std::map<std::string, uint32_t> exports = {
        {"inflateInit2_", kInflateInit2},   {"inflateEnd", kInflateEnd},   {"inflate", kInflate},
        {"HBMCreate", kHbmCreate},          {"HBMDelete", kHbmDelete},     {"HBMInit", kHbmInit},
        {"HBMCalc", kHbmCalc},              {"HBMDraw", kHbmDraw},
        {"HBMGetSelectBtnNum", kHbmGetSelectBtnNum},
        {"HBMSetAdjustFlag", kHbmSetAdjustFlag},
        {"HBMCreateDvdSound", kHbmCreateDvdSound},
        {"HBMUpdateSound", kHbmUpdateSound},
    };
    return exports;
}

std::string ReadGuestString(uint32_t address) {
    std::string text;
    for (uint32_t i = 0; address != 0 && i < 512; ++i) {
        const char c = static_cast<char>(Memory::Read8(address + i));
        if (c == '\0') {
            break;
        }
        text.push_back(c);
    }
    return text;
}

// Guest z_stream (zlib 1.2.x, 32-bit): next_in, avail_in, total_in, next_out,
// avail_out, total_out, msg, state, zalloc, zfree, opaque, data_type, adler, reserved.
enum ZStreamField : uint32_t {
    kNextIn = 0x00, kAvailIn = 0x04, kTotalIn = 0x08, kNextOut = 0x0C, kAvailOut = 0x10,
    kTotalOut = 0x14, kMsg = 0x18, kState = 0x1C, kDataType = 0x2C, kAdler = 0x30,
};

std::mutex g_streamsMutex;
std::map<uint32_t, std::unique_ptr<z_stream>> g_streams;  // keyed by guest z_stream address

} // namespace

extern "C" uint32_t LoadDLL_bw2(uint32_t manager, uint32_t path, uint32_t a2, uint32_t a3, uint32_t a4) {
    (void)manager;
    (void)a2;
    (void)a3;
    (void)a4;
    const std::string name = ReadGuestString(path);
    std::fprintf(stderr, "[bw2] LoadDLL(\"%s\") served natively\n", name.c_str());
    std::memset(Memory::GetPointer(kModuleHandle, 0x40), 0, 0x40);
    return kModuleHandle;
}
PPC_NATIVE_OVERRIDE(8036D7C4, LoadDLL_bw2, uint32_t,
                    (uint32_t manager, uint32_t path, uint32_t a2, uint32_t a3, uint32_t a4),
                    (manager, path, a2, a3, a4));

extern "C" void ImportSymbols_bw2(uint32_t module, uint32_t table, uint32_t count, uint32_t flags) {
    (void)module;
    (void)flags;
    for (uint32_t i = 0; i < count; ++i) {
        const std::string name = ReadGuestString(Memory::Read32(table + i * 8));
        const uint32_t pointerVar = Memory::Read32(table + i * 8 + 4);
        const auto it = Exports().find(name);
        if (it == Exports().end()) {
            std::fprintf(stderr, "[bw2] ImportSymbols: no native export for '%s'\n", name.c_str());
            Memory::Write32(pointerVar, 0);
            continue;
        }
        Memory::Write32(pointerVar, it->second);
    }
}
PPC_NATIVE_OVERRIDE_VOID(8036DB58, ImportSymbols_bw2,
                         (uint32_t module, uint32_t table, uint32_t count, uint32_t flags),
                         (module, table, count, flags));

// ---------------------------------------------------------------- zlib (wiizLib)

extern "C" int32_t inflateInit2_bw2(uint32_t strm, int32_t windowBits, uint32_t version, int32_t streamSize) {
    (void)version;
    (void)streamSize;
    auto stream = std::make_unique<z_stream>();
    std::memset(stream.get(), 0, sizeof(z_stream));
    const int result = ::inflateInit2(stream.get(), windowBits);
    if (result == Z_OK) {
        std::lock_guard<std::mutex> lock(g_streamsMutex);
        g_streams[strm] = std::move(stream);
        Memory::Write32(strm + kState, strm);  // non-null: the guest only tests it
        Memory::Write32(strm + kMsg, 0);
        Memory::Write32(strm + kTotalIn, 0);
        Memory::Write32(strm + kTotalOut, 0);
    }
    return result;
}
REGISTER_NATIVE_FUNCTION(kInflateInit2, inflateInit2_bw2);

extern "C" int32_t inflate_bw2(uint32_t strm, int32_t flush) {
    z_stream* stream = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_streamsMutex);
        const auto it = g_streams.find(strm);
        if (it == g_streams.end()) {
            return Z_STREAM_ERROR;
        }
        stream = it->second.get();
    }
    const uint32_t nextIn = Memory::Read32(strm + kNextIn);
    const uint32_t availIn = Memory::Read32(strm + kAvailIn);
    const uint32_t nextOut = Memory::Read32(strm + kNextOut);
    const uint32_t availOut = Memory::Read32(strm + kAvailOut);
    stream->next_in = availIn ? static_cast<Bytef*>(Memory::GetPointer(nextIn, availIn)) : Z_NULL;
    stream->avail_in = availIn;
    stream->next_out = availOut ? static_cast<Bytef*>(Memory::GetPointer(nextOut, availOut)) : Z_NULL;
    stream->avail_out = availOut;
    const int result = ::inflate(stream, flush);
    const uint32_t consumed = availIn - stream->avail_in;
    const uint32_t produced = availOut - stream->avail_out;
    Memory::Write32(strm + kNextIn, nextIn + consumed);
    Memory::Write32(strm + kAvailIn, stream->avail_in);
    Memory::Write32(strm + kTotalIn, Memory::Read32(strm + kTotalIn) + consumed);
    Memory::Write32(strm + kNextOut, nextOut + produced);
    Memory::Write32(strm + kAvailOut, stream->avail_out);
    Memory::Write32(strm + kTotalOut, Memory::Read32(strm + kTotalOut) + produced);
    Memory::Write32(strm + kDataType, static_cast<uint32_t>(stream->data_type));
    Memory::Write32(strm + kAdler, static_cast<uint32_t>(stream->adler));
    return result;
}
REGISTER_NATIVE_FUNCTION(kInflate, inflate_bw2);

extern "C" int32_t inflateEnd_bw2(uint32_t strm) {
    std::lock_guard<std::mutex> lock(g_streamsMutex);
    const auto it = g_streams.find(strm);
    if (it == g_streams.end()) {
        return Z_STREAM_ERROR;
    }
    ::inflateEnd(it->second.get());
    g_streams.erase(it);
    Memory::Write32(strm + kState, 0);
    return Z_OK;
}
REGISTER_NATIVE_FUNCTION(kInflateEnd, inflateEnd_bw2);

// ---------------------------------------------------------------- HOME button menu (stub)

extern "C" void HBMNoOp_bw2() {}
extern "C" void HBMNoOpArg_bw2(uint32_t) {}
extern "C" int32_t HBMCalc_bw2(uint32_t) { return 0; }  // HBM_SELECT_HOMEBTN: close the menu
extern "C" int32_t HBMGetSelectBtnNum_bw2() { return 0; }
REGISTER_NATIVE_FUNCTION(kHbmCreate, HBMNoOpArg_bw2);
REGISTER_NATIVE_FUNCTION(kHbmDelete, HBMNoOp_bw2);
REGISTER_NATIVE_FUNCTION(kHbmInit, HBMNoOp_bw2);
REGISTER_NATIVE_FUNCTION(kHbmCalc, HBMCalc_bw2);
REGISTER_NATIVE_FUNCTION(kHbmDraw, HBMNoOp_bw2);
REGISTER_NATIVE_FUNCTION(kHbmGetSelectBtnNum, HBMGetSelectBtnNum_bw2);
REGISTER_NATIVE_FUNCTION(kHbmSetAdjustFlag, HBMNoOpArg_bw2);
REGISTER_NATIVE_FUNCTION(kHbmCreateDvdSound, HBMNoOp_bw2);
REGISTER_NATIVE_FUNCTION(kHbmUpdateSound, HBMNoOp_bw2);
