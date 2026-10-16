// The web build's DOL sections. Native builds embed them (generated/
// data_sections_init_blobs.S); the web build ships none of the game's bytes and
// reads them at boot from sys/main.dol under the disc root the page mounted,
// after checking that it is the one supported revision (RBWE01 rev 0).
#include "memory.h"
#include "runtime_config.h"

#include <cryptopp/sha.h>
#include <emscripten/console.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <string>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
// SHA-256 of RBWE01 rev 0 sys/main.dol (the translated code was built from it).
constexpr std::array<uint8_t, 32> kMainDolSha256 = {
    0x9e, 0x55, 0x7a, 0x6c, 0x9d, 0x31, 0x6d, 0xe3, 0xa3, 0xdb, 0x8e, 0x97, 0x90, 0xb8, 0xd8, 0xc5,
    0x4f, 0xfc, 0x9c, 0xd1, 0x65, 0x6d, 0xee, 0x89, 0xa9, 0x0a, 0x2d, 0x7f, 0x96, 0x4d, 0xf8, 0x7f};

uint32_t Be32(const std::vector<uint8_t>& d, size_t at) {
    return (uint32_t{d[at]} << 24) | (uint32_t{d[at + 1]} << 16) | (uint32_t{d[at + 2]} << 8) | d[at + 3];
}

[[noreturn]] void Fail(const char* what) {
    emscripten_err(what);  // straight to the console: stderr may not flush before abort
    std::fprintf(stderr, "[runtime] %s\n", what);
    std::abort();
}
} // namespace

extern "C" void InitializeDataSections() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    const auto path = RuntimeConfigFile::ResolvedDvdRoot() / "sys" / "main.dol";
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        const std::string why = "Cannot open " + path.string() + ": " + std::strerror(errno);
        Fail(why.c_str());
    }
    const std::vector<uint8_t> dol((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (dol.size() < 0x100) Fail("sys/main.dol is truncated");

    std::array<uint8_t, 32> digest{};
    CryptoPP::SHA256().CalculateDigest(digest.data(), dol.data(), dol.size());
    if (digest != kMainDolSha256) Fail("This disc is not the supported one (RBWE01 revision 0): main.dol hash mismatch");

    // 7 text + 11 data sections: file offsets at 0x00, addresses at 0x48, sizes at 0x90.
    for (int i = 0; i < 18; ++i) {
        const uint32_t offset = Be32(dol, 0x00 + i * 4);
        const uint32_t address = Be32(dol, 0x48 + i * 4);
        const uint32_t size = Be32(dol, 0x90 + i * 4);
        if (size == 0) continue;
        if (size_t{offset} + size > dol.size() || !Memory::Contains(address, size)) Fail("Bad DOL section");
        std::memcpy(Memory::GetPointer(address, size), dol.data() + offset, size);
        std::printf("[runtime]   Loaded DOL section %d (%u bytes) @ 0x%08X\n", i, size, address);
    }
}
