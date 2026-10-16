// Keyboard and mouse as a Wii Remote with Nunchuk (channel 0).
//
// BW2 is played with the remote's IR pointer (select and order units) and the
// Nunchuk stick. Without a real remote, KPADRead gets this instead:
//   mouse position in the window -> IR pointer (KPADStatus.pos, dpd_valid_fg)
//   left / right click -> A / B          W A S D -> Nunchuk stick
//   Space / Left Shift -> Z / C          arrows -> D-pad
//   Enter / Backspace -> + / -           1 / 2 -> 1 / 2        H -> HOME
// BW2_AUTOPRESS=1 also holds A+B for 1.5 s every 5 s (unattended boot tests);
// BW2_AUTOPRESS_B_SECONDS=N stops pressing B after N seconds; BW2_AUTOPOINT aims the pointer.
#include "bw2_hle.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <cstdlib>
#include "input_bindings.h"
#include "memory.h"
#include "wii_remote_input.h"

namespace {

constexpr uint32_t kWpadLeft = 0x0001, kWpadRight = 0x0002, kWpadDown = 0x0004, kWpadUp = 0x0008,
                   kWpadPlus = 0x0010, kWpadTwo = 0x0100, kWpadOne = 0x0200, kWpadB = 0x0400, kWpadA = 0x0800,
                   kWpadMinus = 0x1000, kWpadZ = 0x2000, kWpadC = 0x4000, kWpadHome = 0x8000;

// KPADStatus offsets (RVL SDK, 0x84 bytes; BW2's KPADRead uses the same stride).
constexpr uint32_t kPos = 0x20, kVec = 0x28, kSpeed = 0x30, kHorizon = 0x34, kDist = 0x48, kDpdValidFg = 0x5E;

struct Pointer {
    bool valid = false;
    float x = 0.0f;
    float y = 0.0f;
};
Pointer g_pointer;
Pointer g_prevPointer;
bool g_active = false;

bool AutoPressA() {
    static const bool enabled = std::getenv("BW2_AUTOPRESS") != nullptr;
    if (!enabled) {
        return false;
    }
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    return (ms % 5000) < 1500;  // long enough for a slow (occluded-window) frame rate to see it
}

// The title screen wants A and B together; past it B means "back". BW2_AUTOPRESS_B_SECONDS
// limits B to the first N seconds of the run (default: always).
bool AutoPressB() {
    static const char* limit = std::getenv("BW2_AUTOPRESS_B_SECONDS");
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    if (limit != nullptr && std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - start).count() >= std::atol(limit)) {
        return false;
    }
    return AutoPressA();
}

// BW2_AUTOPOINT="x,y;S:x,y;...": where the unattended pointer aims, switching to each
// later x,y once the run is S seconds old (default: the centre).
void AutoPoint(float& x, float& y) {
    const char* spec = std::getenv("BW2_AUTOPOINT");
    if (spec == nullptr) {
        return;
    }
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    const double now = std::chrono::duration<double>(Clock::now() - start).count();
    for (const char* p = spec; *p != '\0';) {
        float at = 0.0f;
        float px = 0.0f;
        float py = 0.0f;
        if (std::sscanf(p, "%f:%f,%f", &at, &px, &py) == 3 || (at = 0.0f, std::sscanf(p, "%f,%f", &px, &py) == 2)) {
            if (now >= at) {
                x = px;
                y = py;
            }
        }
        const char* next = std::strchr(p, ';');
        if (next == nullptr) {
            break;
        }
        p = next + 1;
    }
}

// BW2_AUTOSCRIPT="step;step;...": a scripted run for unattended tests, one step at a
// time (takes precedence over BW2_AUTOPRESS/BW2_AUTOPOINT). Steps:
//   wait=TEXT       until a disc file whose path contains TEXT has been read
//   sleep=SECONDS   pause
//   point=X,Y       aim the pointer (window coordinates, -1..1)
//   press=A|B|AB    hold the buttons 0.25 s, then release for 0.5 s
// Each step is logged as it starts.
struct AutoScript {
    std::vector<std::string> steps;
    size_t index = 0;
    std::chrono::steady_clock::time_point stepStart{};
    bool stepStarted = false;
    bool active = false;
    float x = 0.0f;
    float y = 0.0f;
    uint32_t hold = 0;
};

std::mutex g_seenMutex;
std::vector<std::string> g_seenFiles;

AutoScript& Script() {
    static AutoScript script = [] {
        AutoScript s;
        if (const char* spec = std::getenv("BW2_AUTOSCRIPT")) {
            std::string all(spec);
            size_t at = 0;
            while (at <= all.size()) {
                const size_t end = all.find(';', at);
                const std::string step = all.substr(at, end == std::string::npos ? std::string::npos : end - at);
                if (!step.empty()) s.steps.push_back(step);
                if (end == std::string::npos) break;
                at = end + 1;
            }
            s.active = !s.steps.empty();
        }
        return s;
    }();
    return script;
}

bool SeenFile(const std::string& text) {
    std::lock_guard lock(g_seenMutex);
    for (const auto& path : g_seenFiles) {
        if (path.find(text) != std::string::npos) return true;
    }
    return false;
}

// Advances the script; returns false once it has finished (or was never set).
bool RunAutoScript() {
    AutoScript& s = Script();
    if (!s.active) return false;
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    s.hold = 0;
    while (s.index < s.steps.size()) {
        const std::string& step = s.steps[s.index];
        if (!s.stepStarted) {
            s.stepStarted = true;
            s.stepStart = now;
            std::fprintf(stderr, "[autoscript] step %zu: %s\n", s.index + 1, step.c_str());
        }
        const double elapsed = std::chrono::duration<double>(now - s.stepStart).count();
        const size_t eq = step.find('=');
        const std::string verb = step.substr(0, eq);
        const std::string arg = eq == std::string::npos ? std::string{} : step.substr(eq + 1);
        bool done = true;
        if (verb == "wait") {
            done = SeenFile(arg);
        } else if (verb == "sleep") {
            done = elapsed >= std::atof(arg.c_str());
        } else if (verb == "point") {
            std::sscanf(arg.c_str(), "%f,%f", &s.x, &s.y);
        } else if (verb == "press") {
            const uint32_t buttons = (arg.find('A') != std::string::npos ? kWpadA : 0u) |
                                     (arg.find('B') != std::string::npos ? kWpadB : 0u);
            if (elapsed < 0.25) s.hold = buttons;
            done = elapsed >= 0.75;
        }
        if (!done) return true;
        ++s.index;
        s.stepStarted = false;
    }
    if (s.active) {
        std::fprintf(stderr, "[autoscript] done\n");
        s.active = false;
    }
    return false;
}

} // namespace

void Bw2NoteDvdRead(const char* hostPath) {
    std::lock_guard lock(g_seenMutex);
    g_seenFiles.emplace_back(hostPath);
}

bool Bw2KeyboardRemoteSample(unsigned int chan, WiiRemoteInput::KpadSample& sample) {
    g_active = false;
    if (chan != 0) {
        return false;
    }
    sample = {};
    sample.hasNunchuk = true;
    sample.acc[1] = -1.0f;  // at rest, face up
    sample.nunchukAcc[1] = -1.0f;
    g_active = true;
    g_pointer = {};
    if (InputBindings::InputBlocked()) {
        return true;  // an overlay owns input: connected, idle
    }

    int keyCount = 0;
    const bool* keys = SDL_GetKeyboardState(&keyCount);
    const auto down = [&](SDL_Scancode code) { return keys != nullptr && code < keyCount && keys[code]; };
    const auto bit = [&](bool on, uint32_t value) { if (on) sample.hold |= value; };
    bit(down(SDL_SCANCODE_LEFT), kWpadLeft);
    bit(down(SDL_SCANCODE_RIGHT), kWpadRight);
    bit(down(SDL_SCANCODE_UP), kWpadUp);
    bit(down(SDL_SCANCODE_DOWN), kWpadDown);
    bit(down(SDL_SCANCODE_RETURN), kWpadPlus);
    bit(down(SDL_SCANCODE_BACKSPACE), kWpadMinus);
    bit(down(SDL_SCANCODE_1), kWpadOne);
    bit(down(SDL_SCANCODE_2), kWpadTwo);
    bit(down(SDL_SCANCODE_H), kWpadHome);
    bit(down(SDL_SCANCODE_SPACE), kWpadZ);
    bit(down(SDL_SCANCODE_LSHIFT), kWpadC);
    sample.stick[0] = (down(SDL_SCANCODE_D) ? 1.0f : 0.0f) - (down(SDL_SCANCODE_A) ? 1.0f : 0.0f);
    sample.stick[1] = (down(SDL_SCANCODE_W) ? 1.0f : 0.0f) - (down(SDL_SCANCODE_S) ? 1.0f : 0.0f);

    float mx = 0.0f;
    float my = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
    bit((buttons & SDL_BUTTON_LMASK) != 0 || AutoPressA(), kWpadA);
    bit((buttons & SDL_BUTTON_RMASK) != 0 || AutoPressB(), kWpadB);
    if (SDL_Window* window = SDL_GetMouseFocus()) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window, &w, &h);
        if (w > 0 && h > 0 && mx >= 0 && my >= 0 && mx < w && my < h) {
            g_pointer.valid = true;
            g_pointer.x = mx / static_cast<float>(w) * 2.0f - 1.0f;  // -1 left .. +1 right
            g_pointer.y = my / static_cast<float>(h) * 2.0f - 1.0f;  // -1 top .. +1 bottom
        }
    }
    if (RunAutoScript()) {
        static int frame = 0;  // menus only re-hover when the pointer moves: wobble a little
        g_pointer.valid = true;
        g_pointer.x = Script().x + ((++frame & 1) ? 0.01f : -0.01f);
        g_pointer.y = Script().y;
        sample.hold |= Script().hold;
        return true;
    }
    if (!g_pointer.valid && std::getenv("BW2_AUTOPRESS") != nullptr) {
        g_pointer.valid = true;  // unattended tests: BW2_AUTOPOINT=x,y (default: the centre)
        AutoPoint(g_pointer.x, g_pointer.y);
        static int frame = 0;  // menus only re-hover when the pointer moves: wobble a little
        g_pointer.x += (++frame & 1) ? 0.01f : -0.01f;
    }
    return true;
}

bool Bw2WriteKeyboardRemotePointer(unsigned int chan, unsigned int statusAddr) {
    if (chan != 0 || !g_active) {
        return false;
    }
    const Pointer prev = g_prevPointer;
    g_prevPointer = g_pointer;
    if (!g_pointer.valid) {
        return true;  // pointing away from the screen: pos stays zeroed, dpd_valid_fg 0
    }
    Memory::WriteFloat32(statusAddr + kPos, g_pointer.x);
    Memory::WriteFloat32(statusAddr + kPos + 4, g_pointer.y);
    if (prev.valid) {  // vec/speed: the pointer's motion since the last read, as KPAD reports it
        const float dx = g_pointer.x - prev.x;
        const float dy = g_pointer.y - prev.y;
        Memory::WriteFloat32(statusAddr + kVec, dx);
        Memory::WriteFloat32(statusAddr + kVec + 4, dy);
        Memory::WriteFloat32(statusAddr + kSpeed, std::sqrt(dx * dx + dy * dy));
    }
    Memory::WriteFloat32(statusAddr + kHorizon, 1.0f);  // remote held level
    Memory::WriteFloat32(statusAddr + kHorizon + 4, 0.0f);
    Memory::WriteFloat32(statusAddr + kDist, 2.0f);     // metres from the sensor bar
    Memory::Write8(statusAddr + kDpdValidFg, 2);        // both IR points seen
    return true;
}

bool Bw2KeyboardRemoteConnected(unsigned int chan) {
    return chan == 0 && !WiiRemoteInput::IsRemoteChannel(chan);
}
