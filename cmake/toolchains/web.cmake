# The web build: Emscripten's toolchain targeting wasm64 (MEMORY64, so the
# runtime keeps its 64-bit pointer assumptions) with pthreads and JavaScript
# exceptions for every object (Asyncify, which backs the guest fibers, cannot
# unwind through native WebAssembly try blocks). Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/web.cmake ...
# EMSDK names the Emscripten SDK (default ~/emsdk).
#
# Adapted from mh3-wii (cmake/toolchains/web.cmake, GPLv3,
# https://github.com/Grarak/mh3-wii); see ORIGIN.md.
if(DEFINED ENV{EMSDK})
  set(frontline_emsdk "$ENV{EMSDK}")
else()
  set(frontline_emsdk "$ENV{HOME}/emsdk")
endif()

# Emscripten.cmake picks wasm64 (CMAKE_SIZEOF_VOID_P 8) from CMAKE_C_FLAGS as it
# is when included, so the flags are set before it. SSE intrinsics (-msse4.1)
# are enabled only for the runtime (runtime/CMakeLists.txt): dependencies such as
# abseil would take x86 paths Emscripten cannot compile.
set(frontline_web_flags "-sMEMORY64=1 -pthread -fexceptions -msimd128")
set(CMAKE_C_FLAGS "${frontline_web_flags}" CACHE STRING "")
# fmt uses malloc/free without <cstdlib>, which Emscripten's libc++ does not pull in.
set(CMAKE_CXX_FLAGS "${frontline_web_flags} -include stdlib.h" CACHE STRING "")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${frontline_web_flags}")

include("${frontline_emsdk}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")

# Every library links statically into the one module.
set(BUILD_SHARED_LIBS OFF)
