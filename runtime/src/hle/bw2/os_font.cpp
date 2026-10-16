// RBWE01 OS ROM-font encoding.
//
// OSGetFontEncode/OSSetFontEncode (and OSLoadFont, OSGetFontTexel, ... inline)
// derive the font encoding the first time from the VI DTV status register
// (0xCC00206E) on NTSC consoles. The runtime has no VI register file, so the
// choice is made here once, as an NTSC-U console reports it: ANSI. Every other
// font routine only reads the register while the cached encoding is 0xFFFF, so
// filling the cache here keeps them off the hardware.
#include "hle_stubs.h"

#include <cstdint>
#include <cstring>
#include "memory.h"

namespace {

constexpr uint32_t kFontEncodeAddr = 0x805FC970u;      // -0x47F0(r13), 0xFFFF until computed
constexpr uint32_t kParseStringPtrAddr = 0x805FFEB0u;  // -0x12B0(r13)
constexpr uint32_t kParseStringS = 0x802C4C4Cu;        // set on the first computation
constexpr uint32_t kParseStringW = 0x802C4D48u;        // OSSetFontEncode for encodings 3..5
constexpr uint16_t kFontEncodeAnsi = 0;
constexpr uint16_t kFontEncodeMax = 5;

uint16_t EnsureFontEncode() {
    const uint16_t encode = Memory::Read16(kFontEncodeAddr);
    if (encode != 0xFFFFu) {
        return encode;
    }
    Memory::Write16(kFontEncodeAddr, kFontEncodeAnsi);
    Memory::Write32(kParseStringPtrAddr, kParseStringS);
    return kFontEncodeAnsi;
}

} // namespace

extern "C" uint32_t OSGetFontEncode_bw2() {
    return EnsureFontEncode();
}
PPC_NATIVE_OVERRIDE(802C4750, OSGetFontEncode_bw2, uint32_t, (), ());

extern "C" uint32_t OSSetFontEncode_bw2(uint32_t encode) {
    const uint16_t previous = EnsureFontEncode();
    if (encode <= kFontEncodeMax) {
        Memory::Write16(kFontEncodeAddr, static_cast<uint16_t>(encode));
        if (encode >= 3) {
            Memory::Write32(kParseStringPtrAddr, kParseStringW);
        }
    }
    return previous;
}
PPC_NATIVE_OVERRIDE(802C47A4, OSSetFontEncode_bw2, uint32_t, (uint32_t encode), (encode));

// __OSReadROM(buffer, length, offset) reads the boot ROM over EXI; in this runtime
// that is only the ROM font, which is Nintendo data the port cannot ship. The
// buffer is zero-filled: Yay0 Decode then sees an empty image and ReadFont reports
// no font instead of decoding whatever the stubbed EXI DMA left behind.
extern "C" uint32_t OSReadROM_bw2(uint32_t buffer, int32_t length, int32_t offset) {
    (void)offset;
    if (length > 0 && Memory::Contains(buffer, static_cast<size_t>(length))) {
        std::memset(Memory::GetPointer(buffer, static_cast<size_t>(length)), 0, static_cast<size_t>(length));
    }
    return 1;
}
PPC_NATIVE_OVERRIDE(802C793C, OSReadROM_bw2, uint32_t, (uint32_t buffer, int32_t length, int32_t offset),
                    (buffer, length, offset));

// OSInitFont(OSFontHeader* fontData) for the ANSI encoding. The original reads
// the ROM font (Nintendo data, not shipped), Yay0-decodes it and expands its
// 2bpp sheets to I4. This builds the expanded result directly: an ANSI font
// header, a width table and blank I4 sheets with the ROM font's geometry
// (24x24 cells, 10x10 cells per 256x256 sheet, 0x20..0xFF), so the font API
// and the game's font texture work; glyphs are empty until a free font is
// rasterized into the sheets.
namespace {

constexpr uint32_t kFontDataAnsiPtrAddr = 0x805FFEBCu;  // -0x12A4(r13), set by OSInitFont
constexpr uint16_t kFirstChar = 0x20;
constexpr uint16_t kLastChar = 0xFF;
constexpr uint16_t kCell = 24;
constexpr uint16_t kSheetCells = 10;
constexpr uint16_t kSheetSide = 256;
constexpr uint32_t kSheetBytes = kSheetSide * kSheetSide / 2;  // I4
constexpr uint32_t kGlyphCount = kLastChar - kFirstChar + 1;
constexpr uint32_t kSheetCount = (kGlyphCount + kSheetCells * kSheetCells - 1) / (kSheetCells * kSheetCells);
constexpr uint32_t kWidthTableOffset = 0x30;
constexpr uint32_t kSheetImageOffset = 0x120;  // header + width table, 32-byte aligned (OS_FONT_SIZE_ANSI = 0x20120)

void Put16(uint32_t at, uint16_t value) { Memory::Write16(at, value); }

} // namespace

extern "C" uint32_t OSInitFont_bw2(uint32_t fontData) {
    const uint16_t encode = EnsureFontEncode();
    if (encode != kFontEncodeAnsi || fontData == 0) {
        return 0;  // only the ANSI font is provided (RBWE01 is NTSC-U)
    }
    const uint32_t total = kSheetImageOffset + kSheetCount * kSheetBytes;
    if (!Memory::Contains(fontData, total)) {
        return 0;
    }
    std::memset(Memory::GetPointer(fontData, total), 0, total);
    Put16(fontData + 0x00, 0);            // fontType: ANSI
    Put16(fontData + 0x02, kFirstChar);
    Put16(fontData + 0x04, kLastChar);
    Put16(fontData + 0x06, kFirstChar);   // invalChar: space
    Put16(fontData + 0x08, 18);           // ascent
    Put16(fontData + 0x0A, 6);            // descent
    Put16(fontData + 0x0C, 18);           // width (widest glyph)
    Put16(fontData + 0x0E, 2);            // leading
    Put16(fontData + 0x10, kCell);        // cellWidth
    Put16(fontData + 0x12, kCell);        // cellHeight
    Memory::Write32(fontData + 0x14, kSheetBytes);
    Put16(fontData + 0x18, 0);            // sheetFormat: GX_TF_I4
    Put16(fontData + 0x1A, kSheetCells);  // sheetColumn
    Put16(fontData + 0x1C, kSheetCells);  // sheetRow
    Put16(fontData + 0x1E, kSheetSide);   // sheetWidth
    Put16(fontData + 0x20, kSheetSide);   // sheetHeight
    Put16(fontData + 0x22, kWidthTableOffset);
    Memory::Write32(fontData + 0x24, kSheetImageOffset);
    Memory::Write32(fontData + 0x28, kSheetCount * kSheetBytes);
    for (uint32_t glyph = 0; glyph < kGlyphCount; ++glyph) {
        Memory::Write8(fontData + kWidthTableOffset + glyph, 12);
    }
    Memory::Write32(kFontDataAnsiPtrAddr, fontData);
    return 1;
}
PPC_NATIVE_OVERRIDE(802C5400, OSInitFont_bw2, uint32_t, (uint32_t fontData), (fontData));
