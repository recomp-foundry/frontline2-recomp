// gx_pixel.cpp - Pixel Processing, Blending, and Fog
#include "gx_internal.h"

// ============================================================================
// Blend Mode
// ============================================================================

extern "C" void GX__SetBlendMode_8017277c(uint32_t t, uint32_t s, uint32_t d, uint32_t op) {
    GXSetBlendMode(static_cast<GXBlendMode>(t), static_cast<GXBlendFactor>(s),
                   static_cast<GXBlendFactor>(d), static_cast<GXLogicOp>(op));
}
PPC_NATIVE_OVERRIDE_VOID(802b1048, GX__SetBlendMode_8017277c, (uint32_t t, uint32_t s, uint32_t d, uint32_t op), (t, s, d, op));

extern "C" void GX__SetColorUpdate_801727cc(uint32_t en) {
    GXSetColorUpdate(static_cast<GXBool>(en));
}
PPC_NATIVE_OVERRIDE_VOID(802b1098, GX__SetColorUpdate_801727cc, (uint32_t en), (en));

extern "C" void GX__SetAlphaUpdate_801727f8(uint32_t en) {
    GXSetAlphaUpdate(static_cast<GXBool>(en));
}
PPC_NATIVE_OVERRIDE_VOID(802b10c4, GX__SetAlphaUpdate_801727f8, (uint32_t en), (en));

// ============================================================================
// Z Buffer
// ============================================================================

extern "C" void GX__SetZMode_80172824(uint32_t ce, uint32_t f, uint32_t ue) {
    GXSetZMode(static_cast<GXBool>(ce), static_cast<GXCompare>(f), static_cast<GXBool>(ue));
}
PPC_NATIVE_OVERRIDE_VOID(802B10F0, GX__SetZMode_80172824, (uint32_t ce, uint32_t f, uint32_t ue), (ce, f, ue));

extern "C" void GX__SetZCompLoc_80172858(uint32_t bt) {
    GXSetZCompLoc(static_cast<GXBool>(bt));
}
PPC_NATIVE_OVERRIDE_VOID(802B1124, GX__SetZCompLoc_80172858, (uint32_t bt), (bt));

// ============================================================================
// Pixel Format and Dither
// ============================================================================

extern "C" void GX__SetPixelFmt_80172888(uint32_t pf, uint32_t zf) {
    GXSetPixelFmt(static_cast<GXPixelFmt>(pf), static_cast<GXZFmt16>(zf));
}
PPC_NATIVE_OVERRIDE_VOID(802B1154, GX__SetPixelFmt_80172888, (uint32_t pf, uint32_t zf), (pf, zf));

extern "C" void GX__SetDither_80172930(uint32_t d) {
    GXSetDither(static_cast<GXBool>(d));
}
PPC_NATIVE_OVERRIDE_VOID(802B11FC, GX__SetDither_80172930, (uint32_t d), (d));

extern "C" void GX__SetDstAlpha_8017295c(uint32_t en, uint32_t a) {
    GXSetDstAlpha(static_cast<GXBool>(en), static_cast<u8>(a));
}
PPC_NATIVE_OVERRIDE_VOID(802b1228, GX__SetDstAlpha_8017295c, (uint32_t en, uint32_t a), (en, a));

// ============================================================================
// Fog and Alpha Compare
// ============================================================================

extern "C" void GX__SetFog_801722cc(uint32_t t, float sz, float ez, float nz, float fz, uint32_t cp) {
    GXSetFog((GXFogType)t, sz, ez, nz, fz, DecodeGxColor(Memory::Read32(cp)));
}
PPC_NATIVE_OVERRIDE_VOID(802b0ccc, GX__SetFog_801722cc, (uint32_t t, float sz, float ez, float nz, float fz, uint32_t cp), (t, sz, ez, nz, fz, cp));

extern "C" void GX__SetAlphaCompare_80172088(uint32_t c0, uint32_t r0, uint32_t op, uint32_t c1, uint32_t r1) {
    g_alphaCompareValid = true;
    GXSetAlphaCompare((GXCompare)c0, (u8)r0, (GXAlphaOp)op, (GXCompare)c1, (u8)r1);
}
PPC_NATIVE_OVERRIDE_VOID(802B0A88, GX__SetAlphaCompare_80172088, (uint32_t c0, uint32_t r0, uint32_t op, uint32_t c1, uint32_t r1), (c0, r0, op, c1, r1));

extern "C" void GX__SetZTexture_801720c0(uint32_t op, uint32_t f, uint32_t b) { GXSetZTexture((GXZTexOp)op, (GXTexFmt)f, b); }
PPC_NATIVE_OVERRIDE_VOID(802b0ac0, GX__SetZTexture_801720c0, (uint32_t op, uint32_t f, uint32_t b), (op, f, b));

// ============================================================================
// Culling and Clipping
// ============================================================================

extern "C" void GX__SetCullMode_8016f3b8(uint32_t m) {
    GXSetCullMode(static_cast<GXCullMode>(m));
}
PPC_NATIVE_OVERRIDE_VOID(802ae24c, GX__SetCullMode_8016f3b8, (uint32_t m), (m));

extern "C" void GX__SetCoPlanar_8016f3e0(uint32_t en) { GXSetCoPlanar((GXBool)en); }
PPC_NATIVE_OVERRIDE_VOID(802ae274, GX__SetCoPlanar_8016f3e0, (uint32_t en), (en));

extern "C" void GX__SetClipMode_8017351c(uint32_t m) { GXSetClipMode((GXClipMode)m); }
PPC_NATIVE_OVERRIDE_VOID(802b1914, GX__SetClipMode_8017351c, (uint32_t m), (m));
