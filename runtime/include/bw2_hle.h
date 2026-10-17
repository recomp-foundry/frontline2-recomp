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
// True when channel `chan` is the keyboard/mouse remote (channel 0 with no real
// remote): WPADProbe and WPADGetInfoAsync report it as a remote with Nunchuk.
bool Bw2KeyboardRemoteConnected(unsigned int chan);

// Autoplay script (hle/bw2/keyboard_remote.cpp): the DVD layer reports each
// file's first read, which BW2_AUTOSCRIPT "wait=" steps match against.
void Bw2NoteDvdRead(const char* hostPath);

// Display-list diagnostics (hle/bw2/gx_dl_trace.cpp), used by hle/gx/gx_dl.cpp.
// BW2_DL_TRACE_FRAME=N: Bw2DlTraceFrame() is true while Aurora records frame N;
// Bw2DlTraceLine prints one "[dltrace]" line (printf format).
bool Bw2DlTraceFrame();
void Bw2DlTraceLine(const char* format, ...);
// Route taken by each GX__CallDisplayList (0 regs, 1 flat-verbatim, 2 flat-copy, 3 direct,
// 4 interp, 5 lyt), summarised every 300 frames.
void Bw2DlCountRoute(int route);
// Exceptions the display-list path catches and drops: counted, first one per frame logged.
void Bw2DlNoteSwallowedException(const char* where, const char* what);
// BW2_DL_ENTRY_LAYOUT=0 disables publishing the entry layout (patch 0011) for experiments.
bool Bw2DlEntryLayoutEnabled();

// Spans of the arrays the display list being decoded reads, for the CP array base
// resolver (hle/bw2/gx_array_base.cpp): a base written inside the list resolves to the
// bytes its draws cover. GX__CallDisplayList fills the table before each list and clears it after.
void Bw2ClearDisplayListArraySpans();
void Bw2AddDisplayListArraySpan(unsigned int guestBase, unsigned int size);
