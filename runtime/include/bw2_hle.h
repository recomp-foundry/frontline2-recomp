#pragma once
// Hooks the RBWE01 HLE (runtime/src/hle/bw2/) exposes to upstream runtime files.
struct CpuContext;

// Runs the guest's GX FIFO breakpoint callback once the breakpoint armed by
// GXEnableBreakPt has been "reached" (see runtime/src/hle/bw2/gx_breakpoint.cpp).
void Bw2DeliverGxBreakpoint(CpuContext* ctx);

// GXSetDrawSync records its token here; GXReadDrawSync (hle/bw2/gx_breakpoint.cpp)
// returns it, since the FIFO is consumed as it is written.
void Bw2RecordDrawSyncToken(unsigned int token);


// Keyboard and mouse as a Wii Remote + Nunchuk on channel 0 when no real
// remote is connected (hle/bw2/keyboard_remote.cpp). Fills `sample` (declared in
// wii_remote_input.h) and returns true when it applies.
namespace WiiRemoteInput { struct KpadSample; }
bool Bw2KeyboardRemoteSample(unsigned int chan, WiiRemoteInput::KpadSample& sample);
// Writes KPADStatus.pos/horizon/dist and dpd_valid_fg for the emulated remote's
// mouse pointer; returns false (nothing written) for a real remote.
bool Bw2WriteKeyboardRemotePointer(unsigned int chan, unsigned int statusAddr);

// The GX copy setters are native (Aurora); the originals also keep their BP
// words in __GXData, which BW2's translated GXSetDispCopyYScale reads back.
// These write the same words (hle/bw2/gx_breakpoint.cpp).
void Bw2MirrorCopySrc(bool display, unsigned int left, unsigned int top, unsigned int width, unsigned int height);
void Bw2MirrorDispCopyDst(unsigned int width);
