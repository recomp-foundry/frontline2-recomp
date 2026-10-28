// Keyboard and mouse as a Wii Remote with Nunchuk (channel 0).
//
// BW2 is played with the remote's IR pointer (select and order units) and the
// Nunchuk stick. Without a real remote, KPADRead gets this instead:
//   mouse position in the window -> IR pointer (KPADStatus.pos, dpd_valid_fg)
//   left / right click -> A / B          W A S D -> Nunchuk stick
//   Space / Left Shift -> Z / C          arrows -> D-pad
//   Enter / Backspace -> + / -           1 / 2 -> 1 / 2        H -> HOME
// Those are the defaults; the [wii_remote] section of Config.toml overrides any
// of them (key names as SDL prints them, "mouse_left".."mouse_x2", "" = none)
// and the settings overlay (F10 > Controller settings) edits that section.
// A PC gamepad assigned to port 1 adds to that: left stick = Nunchuk stick,
// right stick moves the pointer (the mouse takes it back as soon as it moves),
// buttons per [wii_remote_pad] (names of controller_button_names.h; defaults
// south/east = A/B, west/north = 1/2, start/back = +/-, guide = HOME,
// left trigger = Z, left bumper = C, d-pad = d-pad).
// BW2_AUTOPRESS=1 also holds A+B for 1.5 s every 5 s (unattended boot tests);
// BW2_AUTOPRESS_B_SECONDS=N stops pressing B after N seconds; BW2_AUTOPOINT aims the pointer.
#include "bw2_hle.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <cstdlib>
#include "controller_button_names.h"
#include "input_bindings.h"
#include "memory.h"
#include "runtime_config.h"
#include "wii_remote_input.h"

#include <dolphin/pad.h>
#include <SDL3/SDL_gamepad.h>

#include <array>
#include <atomic>
#include <fstream>

namespace {

constexpr uint32_t kWpadLeft = 0x0001, kWpadRight = 0x0002, kWpadDown = 0x0004, kWpadUp = 0x0008,
                   kWpadPlus = 0x0010, kWpadTwo = 0x0100, kWpadOne = 0x0200, kWpadB = 0x0400, kWpadA = 0x0800,
                   kWpadMinus = 0x1000, kWpadZ = 0x2000, kWpadC = 0x4000, kWpadHome = 0x8000;

// One row per Bw2RemoteBindings::Input, in enum order. `hold` is the WPAD bit
// the key sets; the stick directions have none and are read by index.
struct RemoteBinding {
    const char* label;
    const char* configKey;
    int defaultKey;
    uint32_t hold;
};
constexpr std::array<RemoteBinding, Bw2RemoteBindings::Count> kRemoteBindings = {{
    {"A", "a", PAD_KEY_MOUSE_LEFT, kWpadA},
    {"B", "b", PAD_KEY_MOUSE_RIGHT, kWpadB},
    {"1", "one", SDL_SCANCODE_1, kWpadOne},
    {"2", "two", SDL_SCANCODE_2, kWpadTwo},
    {"+", "plus", SDL_SCANCODE_RETURN, kWpadPlus},
    {"-", "minus", SDL_SCANCODE_BACKSPACE, kWpadMinus},
    {"HOME", "home", SDL_SCANCODE_H, kWpadHome},
    {"D-pad up", "dpad_up", SDL_SCANCODE_UP, kWpadUp},
    {"D-pad down", "dpad_down", SDL_SCANCODE_DOWN, kWpadDown},
    {"D-pad left", "dpad_left", SDL_SCANCODE_LEFT, kWpadLeft},
    {"D-pad right", "dpad_right", SDL_SCANCODE_RIGHT, kWpadRight},
    {"Nunchuk Z", "z", SDL_SCANCODE_SPACE, kWpadZ},
    {"Nunchuk C", "c", SDL_SCANCODE_LSHIFT, kWpadC},
    {"Stick up", "stick_up", SDL_SCANCODE_W, 0},
    {"Stick down", "stick_down", SDL_SCANCODE_S, 0},
    {"Stick left", "stick_left", SDL_SCANCODE_A, 0},
    {"Stick right", "stick_right", SDL_SCANCODE_D, 0},
}};
// Written by the overlay thread, read by the guest thread: atomics, no lock.
std::array<std::atomic<int>, Bw2RemoteBindings::Count> g_remoteKeys;
std::once_flag g_remoteKeysLoaded;

constexpr std::array<const char*, 5> kMouseKeyNames = {"mouse_left", "mouse_middle", "mouse_right", "mouse_x1", "mouse_x2"};

// Config.toml spelling of a binding: SDL's scancode name, a mouse token or "".
std::string RemoteKeyName(int key) {
    if (key == PAD_KEY_INVALID) {
        return {};
    }
    if (key < PAD_KEY_INVALID) {
        const int button = -key - 2;  // PAD_KEY_MOUSE_LEFT = -2 -> 0
        return button >= 0 && button < static_cast<int>(kMouseKeyNames.size()) ? kMouseKeyNames[button] : std::string{};
    }
    return SDL_GetScancodeName(static_cast<SDL_Scancode>(key));
}

int RemoteKeyFromName(const std::string& name) {
    if (name.empty()) {
        return PAD_KEY_INVALID;
    }
    for (size_t i = 0; i < kMouseKeyNames.size(); ++i) {
        if (name == kMouseKeyNames[i]) {
            return -2 - static_cast<int>(i);
        }
    }
    const SDL_Scancode code = SDL_GetScancodeFromName(name.c_str());
    return code == SDL_SCANCODE_UNKNOWN ? PAD_KEY_INVALID : static_cast<int>(code);
}

// Defaults, then whatever [wii_remote] in Config.toml names (unknown names
// unmap the input so a typo is visible in the overlay rather than silently
// falling back).
void LoadRemoteKeys() {
    for (size_t i = 0; i < kRemoteBindings.size(); ++i) {
        g_remoteKeys[i].store(kRemoteBindings[i].defaultKey, std::memory_order_relaxed);
    }
    const auto path = RuntimeConfigFile::ResolveConfigPath();
    std::ifstream input(path);
    if (!input) {
        return;
    }
    try {
        const toml::value document = toml::parse(input, RuntimeConfigFile::PathToUtf8(path));
        if (!document.contains("wii_remote")) {
            return;
        }
        const toml::value& section = toml::find(document, "wii_remote");
        for (size_t i = 0; i < kRemoteBindings.size(); ++i) {
            if (section.contains(kRemoteBindings[i].configKey)) {
                const auto name = toml::find<std::string>(section, kRemoteBindings[i].configKey);
                g_remoteKeys[i].store(RemoteKeyFromName(name), std::memory_order_relaxed);
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[bw2] [wii_remote] in Config.toml ignored: %s\n", e.what());
    }
}

int RemoteKey(unsigned int input) {
    std::call_once(g_remoteKeysLoaded, LoadRemoteKeys);
    return input < Bw2RemoteBindings::Count ? g_remoteKeys[input].load(std::memory_order_relaxed) : PAD_KEY_INVALID;
}

void StoreRemoteKey(unsigned int input, int key) {
    if (input >= Bw2RemoteBindings::Count) {
        return;
    }
    std::call_once(g_remoteKeysLoaded, LoadRemoteKeys);
    g_remoteKeys[input].store(key, std::memory_order_relaxed);
    RuntimeConfigFile::WriteSetting("wii_remote", kRemoteBindings[input].configKey,
                                    RuntimeConfigFile::FormatString(RemoteKeyName(key)));
}

// ---- PC gamepad as the same remote ([wii_remote_pad]) ----

constexpr std::array<uint32_t, Bw2RemotePadBindings::Count> kPadDefaults = {
    SDL_GAMEPAD_BUTTON_SOUTH,                                      // A
    SDL_GAMEPAD_BUTTON_EAST,                                       // B
    SDL_GAMEPAD_BUTTON_WEST,                                       // 1
    SDL_GAMEPAD_BUTTON_NORTH,                                      // 2
    SDL_GAMEPAD_BUTTON_START,                                      // +
    SDL_GAMEPAD_BUTTON_BACK,                                       // -
    SDL_GAMEPAD_BUTTON_GUIDE,                                      // HOME
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    PADEncodeAxisButton(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, false),      // Z
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,                              // C
};
constexpr float kDefaultPointerSpeed = 2.5f;
constexpr float kStickDeadZone = 0.15f;
std::array<std::atomic<uint32_t>, Bw2RemotePadBindings::Count> g_padButtons;
std::atomic<float> g_pointerSpeed{kDefaultPointerSpeed};
std::once_flag g_padBindingsLoaded;

void LoadPadBindings() {
    for (size_t i = 0; i < kPadDefaults.size(); ++i) {
        g_padButtons[i].store(kPadDefaults[i], std::memory_order_relaxed);
    }
    const auto path = RuntimeConfigFile::ResolveConfigPath();
    std::ifstream input(path);
    if (!input) {
        return;
    }
    try {
        const toml::value document = toml::parse(input, RuntimeConfigFile::PathToUtf8(path));
        if (!document.contains("wii_remote_pad")) {
            return;
        }
        const toml::value& section = toml::find(document, "wii_remote_pad");
        for (size_t i = 0; i < kPadDefaults.size(); ++i) {
            const char* key = kRemoteBindings[i].configKey;
            if (!section.contains(key)) {
                continue;
            }
            std::string name = toml::find<std::string>(section, key);
            name = name.substr(0, name.find('@'));  // the overlay's "@threshold" suffix
            const ControllerNames::NativeButtonItem* item = ControllerNames::FindNativeButton(ControllerNames::TrimToken(name));
            g_padButtons[i].store(item != nullptr ? item->nativeButton : PAD_NATIVE_BUTTON_DISABLED, std::memory_order_relaxed);
        }
        if (section.contains("pointer_speed")) {
            const double speed = toml::find<double>(section, "pointer_speed");
            if (speed > 0.0 && speed < 100.0) {
                g_pointerSpeed.store(static_cast<float>(speed), std::memory_order_relaxed);
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[bw2] [wii_remote_pad] in Config.toml ignored: %s\n", e.what());
    }
}

uint32_t PadBinding(unsigned int input) {
    std::call_once(g_padBindingsLoaded, LoadPadBindings);
    return input < Bw2RemotePadBindings::Count ? g_padButtons[input].load(std::memory_order_relaxed) : PAD_NATIVE_BUTTON_DISABLED;
}

void StorePadBinding(unsigned int input, uint32_t value) {
    if (input >= Bw2RemotePadBindings::Count) {
        return;
    }
    std::call_once(g_padBindingsLoaded, LoadPadBindings);
    g_padButtons[input].store(value, std::memory_order_relaxed);
    RuntimeConfigFile::WriteSetting("wii_remote_pad", kRemoteBindings[input].configKey,
                                    RuntimeConfigFile::FormatString(ControllerNames::NativeButtonForValue(value).configName));
}

// The SDL gamepad on game port 0 unless it is a Wii controller (those reach
// KPAD through WiiRemoteInput before this file is consulted).
SDL_Gamepad* RemotePad() {
    const int32_t index = PADGetIndexForPort(0);
    if (index < 0 || WiiRemoteInput::IsRemoteChannel(0)) {
        return nullptr;
    }
    return PADGetSDLGamepadForIndex(static_cast<uint32_t>(index));
}

// A native binding as the overlay encodes it: an SDL button, or an axis past
// its threshold (PADEncodeAxisButton).
bool PadPressed(SDL_Gamepad* pad, uint32_t binding) {
    if (binding == PAD_NATIVE_BUTTON_DISABLED || binding == PAD_NATIVE_BUTTON_INVALID) {
        return false;
    }
    if (PADIsAxisButton(binding)) {
        const uint32_t axis = PADAxisButtonAxis(binding);
        if (axis >= SDL_GAMEPAD_AXIS_COUNT) {
            return false;
        }
        const int value = SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(axis));
        const int threshold = static_cast<int>(PADAxisButtonThreshold(binding)) * 32767 / 100;
        return PADAxisButtonNegative(binding) ? value <= -threshold : value >= threshold;
    }
    return binding < SDL_GAMEPAD_BUTTON_COUNT && SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(binding));
}

// Stick with a radial dead zone, rescaled so the edge of the dead zone is 0; SDL's +y is down.
void PadStick(SDL_Gamepad* pad, SDL_GamepadAxis xAxis, SDL_GamepadAxis yAxis, float& x, float& y) {
    x = SDL_GetGamepadAxis(pad, xAxis) / 32767.0f;
    y = -SDL_GetGamepadAxis(pad, yAxis) / 32767.0f;
    const float magnitude = std::sqrt(x * x + y * y);
    if (magnitude <= kStickDeadZone) {
        x = y = 0.0f;
        return;
    }
    const float scale = std::min((magnitude - kStickDeadZone) / (1.0f - kStickDeadZone), 1.0f) / magnitude;
    x *= scale;
    y *= scale;
}

// Pointer driven by the right stick; the mouse owns it again as soon as it moves.
struct PadPointer {
    float x = 0.0f;
    float y = 0.0f;
    bool active = false;
    float lastMouseX = -1.0f;
    float lastMouseY = -1.0f;
    std::chrono::steady_clock::time_point last{};
} g_padPointer;

} // namespace

namespace Bw2RemoteBindings {
const char* Label(unsigned int input) { return input < Count ? kRemoteBindings[input].label : ""; }
int Binding(unsigned int input) { return RemoteKey(input); }
void SetBinding(unsigned int input, int key) { StoreRemoteKey(input, key); }
void ResetBindings() {
    for (unsigned int i = 0; i < Count; ++i) {
        StoreRemoteKey(i, kRemoteBindings[i].defaultKey);
    }
}
} // namespace Bw2RemoteBindings

namespace Bw2RemotePadBindings {
bool Connected() { return RemotePad() != nullptr; }
unsigned int Binding(unsigned int input) { return PadBinding(input); }
void SetBinding(unsigned int input, unsigned int nativeButton) { StorePadBinding(input, nativeButton); }
void ResetBindings() {
    for (unsigned int i = 0; i < Count; ++i) {
        StorePadBinding(i, kPadDefaults[i]);
    }
    SetPointerSpeed(kDefaultPointerSpeed, true);
}
float PointerSpeed() {
    std::call_once(g_padBindingsLoaded, LoadPadBindings);
    return g_pointerSpeed.load(std::memory_order_relaxed);
}
void SetPointerSpeed(float speed, bool persist) {
    speed = std::clamp(speed, 0.1f, 20.0f);
    g_pointerSpeed.store(speed, std::memory_order_relaxed);
    if (persist) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.2f", speed);
        RuntimeConfigFile::WriteSetting("wii_remote_pad", "pointer_speed", text);
    }
}
} // namespace Bw2RemotePadBindings

namespace {

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
    float stickX = 0.0f;  // "stick=x,y": Nunchuk stick held until the next stick= step
    float stickY = 0.0f;
    bool stickAuto = false;
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

// Button letters of a "press=" / "hold=" step: A B Z C 1 2 + - H(ome) and U D L R (D-pad).
uint32_t ScriptButtons(const std::string& names) {
    uint32_t buttons = 0;
    for (const char c : names) {
        switch (c) {
        case 'A': buttons |= kWpadA; break;
        case 'B': buttons |= kWpadB; break;
        case 'Z': buttons |= kWpadZ; break;
        case 'C': buttons |= kWpadC; break;
        case '1': buttons |= kWpadOne; break;
        case '2': buttons |= kWpadTwo; break;
        case '+': buttons |= kWpadPlus; break;
        case '-': buttons |= kWpadMinus; break;
        case 'H': buttons |= kWpadHome; break;
        case 'U': buttons |= kWpadUp; break;
        case 'D': buttons |= kWpadDown; break;
        case 'L': buttons |= kWpadLeft; break;
        case 'R': buttons |= kWpadRight; break;
        default: break;
        }
    }
    return buttons;
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
        } else if (verb == "stick") {
            // "stick=auto": steer toward the tracked billboard (AURORA_TRACK_BILLBOARD, the
            // objective star) each frame; turn in place while it is off screen.
            s.stickAuto = arg == "auto";
            if (!s.stickAuto) std::sscanf(arg.c_str(), "%f,%f", &s.stickX, &s.stickY);
        } else if (verb == "press") {
            // "press=AB": the named buttons for 0.25 s, then 0.5 s released.
            if (elapsed < 0.25) s.hold = ScriptButtons(arg);
            done = elapsed >= 0.75;
        } else if (verb == "hold") {
            // "hold=ZB,3": the named buttons held for that many seconds.
            const size_t comma = arg.find(',');
            const double seconds = comma == std::string::npos ? 1.0 : std::atof(arg.c_str() + comma + 1);
            if (elapsed < seconds) s.hold = ScriptButtons(arg.substr(0, comma));
            done = elapsed >= seconds;
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
    float mx = 0.0f;
    float my = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
    // A binding is a scancode or, below PAD_KEY_INVALID, a mouse button (-2 = left).
    const auto pressed = [&](unsigned int input) {
        const int key = RemoteKey(input);
        if (key == PAD_KEY_INVALID) return false;
        if (key < PAD_KEY_INVALID) return (buttons & SDL_BUTTON_MASK(-key - 1)) != 0;
        return keys != nullptr && key < keyCount && keys[key];
    };
    const auto bit = [&](bool on, uint32_t value) { if (on) sample.hold |= value; };
    for (unsigned int i = 0; i < Bw2RemoteBindings::Count; ++i) {
        bit(kRemoteBindings[i].hold != 0 && pressed(i), kRemoteBindings[i].hold);
    }
    const auto axis = [&](unsigned int plus, unsigned int minus) {
        return (pressed(plus) ? 1.0f : 0.0f) - (pressed(minus) ? 1.0f : 0.0f);
    };
    sample.stick[0] = axis(Bw2RemoteBindings::StickRight, Bw2RemoteBindings::StickLeft);
    sample.stick[1] = axis(Bw2RemoteBindings::StickUp, Bw2RemoteBindings::StickDown);
    bit(AutoPressA(), kWpadA);
    bit(AutoPressB(), kWpadB);
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
    if (SDL_Gamepad* pad = RemotePad()) {
        for (unsigned int i = 0; i < Bw2RemotePadBindings::Count; ++i) {
            bit(PadPressed(pad, PadBinding(i)), kRemoteBindings[i].hold);
        }
        float lx = 0.0f, ly = 0.0f;
        PadStick(pad, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, lx, ly);
        if (lx != 0.0f || ly != 0.0f) {
            sample.stick[0] = lx;
            sample.stick[1] = ly;
        }
        const auto now = std::chrono::steady_clock::now();
        const float dt = g_padPointer.last.time_since_epoch().count() == 0
                             ? 0.0f
                             : std::min(std::chrono::duration<float>(now - g_padPointer.last).count(), 0.1f);
        g_padPointer.last = now;
        if (mx != g_padPointer.lastMouseX || my != g_padPointer.lastMouseY) {
            g_padPointer.lastMouseX = mx;
            g_padPointer.lastMouseY = my;
            g_padPointer.active = false;  // the mouse moved: it owns the pointer
        }
        if (!g_padPointer.active && g_pointer.valid) {
            g_padPointer.x = g_pointer.x;  // start from wherever the mouse left it
            g_padPointer.y = g_pointer.y;
        }
        float rx = 0.0f, ry = 0.0f;
        PadStick(pad, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, rx, ry);
        if (rx != 0.0f || ry != 0.0f) {
            const float speed = Bw2RemotePadBindings::PointerSpeed();
            g_padPointer.x = std::clamp(g_padPointer.x + rx * speed * dt, -1.0f, 1.0f);
            g_padPointer.y = std::clamp(g_padPointer.y - ry * speed * dt, -1.0f, 1.0f);  // pointer +y is down
            g_padPointer.active = true;
        }
        if (g_padPointer.active) {
            g_pointer.valid = true;
            g_pointer.x = g_padPointer.x;
            g_pointer.y = g_padPointer.y;
        }
    }
    if (RunAutoScript()) {
        static int frame = 0;  // menus only re-hover when the pointer moves: wobble a little
        g_pointer.valid = true;
        g_pointer.x = Script().x + ((++frame & 1) ? 0.01f : -0.01f);
        g_pointer.y = Script().y;
        sample.hold |= Script().hold;
        if (Script().stickAuto) {
            // Minimap steering: AURORA_TRACK_HUD="32x32,14;16x16,9" = objective star blip and
            // player blip. The minimap keeps the camera forward up, so the blip offset is the
            // direction to walk: x to turn, y (up) to advance.
            float sx = 0.0f, sy = 0.0f, px = 0.0f, py = 0.0f;
            const uint32_t starAge = aurora_tracked_hud(0, &sx, &sy);
            const uint32_t playerAge = aurora_tracked_hud(1, &px, &py);
            float dx = 0.0f, dy = 0.0f;
            bool fresh = false;
            if (starAge <= 30 && playerAge <= 30) {
                dx = (sx - px) * 320.0f;  // pixels
                dy = (sy - py) * 240.0f;
                fresh = true;
            }
            static int logCountdown = 0;
            if (--logCountdown <= 0) {
                logCountdown = 60;
                std::fprintf(stderr, "[autostick] star=(%.2f,%.2f) age=%u player=(%.2f,%.2f) age=%u d=(%.0f,%.0f)\n",
                             sx, sy, starAge, px, py, playerAge, dx, dy);
            }
            if (fresh) {
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len > 2.0f) {
                    sample.stick[0] = dx / len;
                    sample.stick[1] = dy / len;
                } else {
                    sample.stick[0] = 0.0f;
                    sample.stick[1] = 0.0f;
                }
            } else {
                sample.stick[0] = 0.0f;  // no minimap: walk straight
                sample.stick[1] = 1.0f;
            }
        } else {
            sample.stick[0] = Script().stickX;
            sample.stick[1] = Script().stickY;
        }
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
