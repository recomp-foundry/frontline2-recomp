// The web build's files, set up when main() starts:
//
//   /disc   the extracted disc (sys/, files/): Module.frontlineDisc lists it and
//           reads it (frontline_disc.js; frontline_web.js holds the backend).
//   /app    the runtime's own assets (--preload-file), the "executable directory"
//           through /proc/self/exe.
//   /data   the application data directory (XDG_DATA_HOME): Config.toml, NAND.
//           Written before the static constructors, which already read it.
//
// Adapted from mh3-wii (runtime/web/web_files.cpp, GPLv3); see ORIGIN.md.
#include <emscripten/em_js.h>
#include <emscripten/wasmfs.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

void Fail(const std::string& what) {
    std::fprintf(stderr, "[web] %s\n", what.c_str());
    std::abort();
}

void Directory(const std::string& path) {
    if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST) Fail("Cannot create " + path + ": " + std::strerror(errno));
}

void Directories(const std::string& path) {
    for (size_t at = path.find('/', 1); at != std::string::npos; at = path.find('/', at + 1)) Directory(path.substr(0, at));
    Directory(path);
}

extern "C" void frontline_disc_backend(backend_t backend);
extern "C" void frontline_disc_pending(const char* path, int size);

// Two steps: an awaited call returning only an int (a pointer result would be a
// 64-bit value Asyncify's resume path cannot hand back on wasm64), then a copy.
EM_ASYNC_JS(int, frontline_disc_list_fetch, (), {
    Module.frontlineListing = await Module.frontlineDisc.list();
    return lengthBytesUTF8(Module.frontlineListing) + 1;
});
EM_JS(void, frontline_disc_list_copy, (char* out, int size), {
    stringToUTF8(Module.frontlineListing, Number(out), size);  // wasm64: pointers arrive as BigInt
    delete Module.frontlineListing;
});

void MountDisc() {
    backend_t disc = wasmfs_create_jsimpl_backend();
    frontline_disc_backend(disc);
    if (wasmfs_create_directory("/disc", 0777, disc) != 0) Fail("Cannot mount the disc");

    std::string listing(static_cast<size_t>(frontline_disc_list_fetch()), '\0');
    frontline_disc_list_copy(listing.data(), static_cast<int>(listing.size()));
    std::istringstream manifest(listing.c_str());
    std::string line;
    size_t files = 0;
    while (std::getline(manifest, line)) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string relative = line.substr(tab + 1);
        const std::string path = "/disc/" + relative;
        const size_t slash = path.rfind('/');
        if (slash > 5) Directories(path.substr(0, slash));
        frontline_disc_pending(relative.c_str(), std::atoi(line.c_str()));
        const int fd = open(path.c_str(), O_CREAT | O_WRONLY, 0444);
        if (fd < 0) Fail("Cannot create " + path);
        close(fd);
        ++files;
    }
    std::fprintf(stderr, "[web] disc: %zu files\n", files);
}

} // namespace

// The data directory and Config.toml, before the static constructors (priority
// 101 runs ahead of them) first read the configuration.
__attribute__((constructor(101))) void SetUpWebConfig() {
    setenv("XDG_DATA_HOME", "/data", 1);
    // SDL's Emscripten audio drivers (even "dummy") are not wasm64-safe yet: their
    // JavaScript mixes BigInt pointers. Naming no real driver makes SDL's audio
    // init fail cleanly, which the runtime tolerates: the web build runs silent.
    setenv("SDL_AUDIO_DRIVER", "none", 1);
    Directories("/data/FrontlineRecomp");
    std::ofstream config("/data/FrontlineRecomp/Config.toml");
    config << "[paths]\ndvd_root = \"/disc\"\n\n"
              "[video]\ngraphics_api = \"webgpu\"\n\n"
              "[network]\nenabled = false\n\n"
              "[discord]\nenabled = false\n";
}

// Called first thing in main(): the disc listing is awaited with Asyncify, which
// static constructors cannot do.
void SetUpWebFiles() {
    MountDisc();

    Directories("/proc/self");
    if (symlink("/app/FrontlineRecomp", "/proc/self/exe") != 0 && errno != EEXIST) Fail("Cannot link /proc/self/exe");
}
