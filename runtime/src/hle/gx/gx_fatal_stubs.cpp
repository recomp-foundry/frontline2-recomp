// Auto-generated GX fatal stubs (projects/bw2/tools/remap_runtime.py)
#include "hle_stubs.h"
#include "runtime_log.h"

namespace {
[[noreturn]] void HaltGX(uint32_t addr, const char* name) {
    const char* symbol = name ? name : "<unknown GX symbol>";
    RT_LOGF(RT_TAG_GX,
            "unimplemented GX entry point: %s at guest address 0x%08X.\n"
            "[gx] This graphics call has no Aurora implementation bound to it yet, so the\n"
            "[gx] runtime cannot continue without silently dropping GPU state. Bind it in\n"
            "[gx] runtime/src/hle/gx/ and remove the stub from gx_fatal_stubs.cpp.\n",
            symbol, addr);
    std::fflush(stderr);
    char message[512]{};
    std::snprintf(message, sizeof(message),
                  "%s at guest address 0x%08X has no Aurora implementation bound to it, so the "
                  "runtime stopped rather than keep rendering with missing GPU state.",
                  symbol, addr);
    ShowRuntimeFatalPopup("the game called an unimplemented graphics function", message);
    std::abort();
}
} // namespace

// Every fatal stub is the same two statements with the address and the symbol
// name substituted, so the body comes from this macro. The registration is
// deliberately still spelled out per entry so the translator's runtime-native
// index sees the literal PPC_NATIVE_OVERRIDE_VOID invocation. Hiding it inside
// this macro would leave the index unable to associate an address with the stub.
#define GX_FATAL_STUB(addr, sym) \
    extern "C" void gx_stub_##addr(CpuContext* ctx) { (void)ctx; HaltGX(0x##addr, sym); }

GX_FATAL_STUB(802aa7e8, "__GX__DefaultTexRegionCallback") PPC_NATIVE_OVERRIDE_VOID(802aa7e8, gx_stub_802aa7e8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802aa8dc, "__GX__DefaultTlutRegionCallback") PPC_NATIVE_OVERRIDE_VOID(802aa8dc, gx_stub_802aa8dc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802aa900, "__GX__Shutdown") PPC_NATIVE_OVERRIDE_VOID(802aa900, gx_stub_802aa900, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ab9b0, "GX__CPInterruptHandler") PPC_NATIVE_OVERRIDE_VOID(802ab9b0, gx_stub_802ab9b0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ac46c, "__GX__CleanGPFifo") PPC_NATIVE_OVERRIDE_VOID(802ac46c, gx_stub_802ac46c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ac8c4, "__GX__SetVCD") PPC_NATIVE_OVERRIDE_VOID(802ac8c4, gx_stub_802ac8c4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ac974, "__GX__CalculateVLim") PPC_NATIVE_OVERRIDE_VOID(802ac974, gx_stub_802ac974, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ace38, "__GX__SetVAT") PPC_NATIVE_OVERRIDE_VOID(802ace38, gx_stub_802ace38, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ad550, "__GX__Abort") PPC_NATIVE_OVERRIDE_VOID(802ad550, gx_stub_802ad550, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ad6b4, "GX__AbortFrame") PPC_NATIVE_OVERRIDE_VOID(802ad6b4, gx_stub_802ad6b4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ada0c, "GX__PokeAlphaMode") PPC_NATIVE_OVERRIDE_VOID(802ada0c, gx_stub_802ada0c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ada48, "GX__PokeBlendMode") PPC_NATIVE_OVERRIDE_VOID(802ada48, gx_stub_802ada48, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802adaa4, "GX__PokeColorUpdate") PPC_NATIVE_OVERRIDE_VOID(802adaa4, gx_stub_802adaa4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802adab8, "GX__PokeDstAlpha") PPC_NATIVE_OVERRIDE_VOID(802adab8, gx_stub_802adab8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802adad0, "GX__PokeDither") PPC_NATIVE_OVERRIDE_VOID(802adad0, gx_stub_802adad0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802adae4, "GX__PokeZMode") PPC_NATIVE_OVERRIDE_VOID(802adae4, gx_stub_802adae4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ae0b4, "__GX__SendFlushPrim") PPC_NATIVE_OVERRIDE_VOID(802ae0b4, gx_stub_802ae0b4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b14cc, "__GX__SetProjection") PPC_NATIVE_OVERRIDE_VOID(802b14cc, gx_stub_802b14cc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b173c, "__GX__SetViewport") PPC_NATIVE_OVERRIDE_VOID(802b173c, gx_stub_802b173c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b193c, "__GX__SetMatrixIndex") PPC_NATIVE_OVERRIDE_VOID(802b193c, gx_stub_802b193c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b19c4, "GX__SetGPMetric") PPC_NATIVE_OVERRIDE_VOID(802b19c4, gx_stub_802b19c4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b21f0, "GX__ClearGPMetric") PPC_NATIVE_OVERRIDE_VOID(802b21f0, gx_stub_802b21f0, (CpuContext* ctx), (ctx));
