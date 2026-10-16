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
GX_FATAL_STUB(802ab0a0, "__GXInitGX") PPC_NATIVE_OVERRIDE_VOID(802ab0a0, gx_stub_802ab0a0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ab9b0, "GX__CPInterruptHandler") PPC_NATIVE_OVERRIDE_VOID(802ab9b0, gx_stub_802ab9b0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802abb10, "GXInitFifoBase") PPC_NATIVE_OVERRIDE_VOID(802abb10, gx_stub_802abb10, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ac3f0, "__GXFifoInit") PPC_NATIVE_OVERRIDE_VOID(802ac3f0, gx_stub_802ac3f0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ac46c, "__GX__CleanGPFifo") PPC_NATIVE_OVERRIDE_VOID(802ac46c, gx_stub_802ac46c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ac60c, "GXSetCurrentGXThread") PPC_NATIVE_OVERRIDE_VOID(802ac60c, gx_stub_802ac60c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ac8c4, "__GX__SetVCD") PPC_NATIVE_OVERRIDE_VOID(802ac8c4, gx_stub_802ac8c4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ac974, "__GX__CalculateVLim") PPC_NATIVE_OVERRIDE_VOID(802ac974, gx_stub_802ac974, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ace38, "__GX__SetVAT") PPC_NATIVE_OVERRIDE_VOID(802ace38, gx_stub_802ace38, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ad1d8, "GXInvalidateVtxCache") PPC_NATIVE_OVERRIDE_VOID(802ad1d8, gx_stub_802ad1d8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ad550, "__GX__Abort") PPC_NATIVE_OVERRIDE_VOID(802ad550, gx_stub_802ad550, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ad6b4, "GX__AbortFrame") PPC_NATIVE_OVERRIDE_VOID(802ad6b4, gx_stub_802ad6b4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ada0c, "GX__PokeAlphaMode") PPC_NATIVE_OVERRIDE_VOID(802ada0c, gx_stub_802ada0c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ada1c, "GXPokeAlphaRead") PPC_NATIVE_OVERRIDE_VOID(802ada1c, gx_stub_802ada1c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ada48, "GX__PokeBlendMode") PPC_NATIVE_OVERRIDE_VOID(802ada48, gx_stub_802ada48, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802adaa4, "GX__PokeColorUpdate") PPC_NATIVE_OVERRIDE_VOID(802adaa4, gx_stub_802adaa4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802adab8, "GX__PokeDstAlpha") PPC_NATIVE_OVERRIDE_VOID(802adab8, gx_stub_802adab8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802adad0, "GX__PokeDither") PPC_NATIVE_OVERRIDE_VOID(802adad0, gx_stub_802adad0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802adae4, "GX__PokeZMode") PPC_NATIVE_OVERRIDE_VOID(802adae4, gx_stub_802adae4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802adb00, "GXSetDrawSyncCallback") PPC_NATIVE_OVERRIDE_VOID(802adb00, gx_stub_802adb00, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802adb44, "GXTokenInterruptHandler") PPC_NATIVE_OVERRIDE_VOID(802adb44, gx_stub_802adb44, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802adbc8, "GXSetDrawDoneCallback") PPC_NATIVE_OVERRIDE_VOID(802adbc8, gx_stub_802adbc8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ae0b4, "__GX__SendFlushPrim") PPC_NATIVE_OVERRIDE_VOID(802ae0b4, gx_stub_802ae0b4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ae4b0, "GXGetYScaleFactor") PPC_NATIVE_OVERRIDE_VOID(802ae4b0, gx_stub_802ae4b0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ae6e0, "GXSetDispCopyYScale") PPC_NATIVE_OVERRIDE_VOID(802ae6e0, gx_stub_802ae6e0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802ae7ac, "GXSetCopyClear") PPC_NATIVE_OVERRIDE_VOID(802ae7ac, gx_stub_802ae7ac, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802aece8, "GXInitLightAttn") PPC_NATIVE_OVERRIDE_VOID(802aece8, gx_stub_802aece8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802aed04, "GXInitLightDistAttn") PPC_NATIVE_OVERRIDE_VOID(802aed04, gx_stub_802aed04, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802aeef0, "GXInitLightColor") PPC_NATIVE_OVERRIDE_VOID(802aeef0, gx_stub_802aeef0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802af1f0, "GXGetTexBufferSize") PPC_NATIVE_OVERRIDE_VOID(802af1f0, gx_stub_802af1f0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802af8a0, "GXGetTexObjUserData") PPC_NATIVE_OVERRIDE_VOID(802af8a0, gx_stub_802af8a0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802af8a8, "GXGetTexObjWidth") PPC_NATIVE_OVERRIDE_VOID(802af8a8, gx_stub_802af8a8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802af8bc, "GXGetTexObjHeight") PPC_NATIVE_OVERRIDE_VOID(802af8bc, gx_stub_802af8bc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802af8d0, "GXGetTexObjFmt") PPC_NATIVE_OVERRIDE_VOID(802af8d0, gx_stub_802af8d0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802af8d8, "GXGetTexObjWrapS") PPC_NATIVE_OVERRIDE_VOID(802af8d8, gx_stub_802af8d8, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802af8e4, "GXGetTexObjWrapT") PPC_NATIVE_OVERRIDE_VOID(802af8e4, gx_stub_802af8e4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802af8f0, "GXGetTexObjMipMap") PPC_NATIVE_OVERRIDE_VOID(802af8f0, gx_stub_802af8f0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802afb70, "GXInitTexCacheRegion") PPC_NATIVE_OVERRIDE_VOID(802afb70, gx_stub_802afb70, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802afc24, "GXInitTlutRegion") PPC_NATIVE_OVERRIDE_VOID(802afc24, gx_stub_802afc24, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802afca0, "GXSetTlutRegionCallback") PPC_NATIVE_OVERRIDE_VOID(802afca0, gx_stub_802afca0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b0f24, "GXSetFogRangeAdj") PPC_NATIVE_OVERRIDE_VOID(802b0f24, gx_stub_802b0f24, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b1258, "GXSetFieldMask") PPC_NATIVE_OVERRIDE_VOID(802b1258, gx_stub_802b1258, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b128c, "GXSetFieldMode") PPC_NATIVE_OVERRIDE_VOID(802b128c, gx_stub_802b128c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b14cc, "__GX__SetProjection") PPC_NATIVE_OVERRIDE_VOID(802b14cc, gx_stub_802b14cc, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b173c, "__GX__SetViewport") PPC_NATIVE_OVERRIDE_VOID(802b173c, gx_stub_802b173c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b1890, "GXGetScissor") PPC_NATIVE_OVERRIDE_VOID(802b1890, gx_stub_802b1890, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b193c, "__GX__SetMatrixIndex") PPC_NATIVE_OVERRIDE_VOID(802b193c, gx_stub_802b193c, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b19c4, "GX__SetGPMetric") PPC_NATIVE_OVERRIDE_VOID(802b19c4, gx_stub_802b19c4, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(802b21f0, "GX__ClearGPMetric") PPC_NATIVE_OVERRIDE_VOID(802b21f0, gx_stub_802b21f0, (CpuContext* ctx), (ctx));
