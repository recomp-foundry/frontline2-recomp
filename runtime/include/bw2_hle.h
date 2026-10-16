#pragma once
// Hooks the RBWE01 HLE (runtime/src/hle/bw2/) exposes to upstream runtime files.
struct CpuContext;

// Runs the guest's GX FIFO breakpoint callback once the breakpoint armed by
// GXEnableBreakPt has been "reached" (see runtime/src/hle/bw2/gx_breakpoint.cpp).
void Bw2DeliverGxBreakpoint(CpuContext* ctx);

// GXSetDrawSync records its token here; GXReadDrawSync (hle/bw2/gx_breakpoint.cpp)
// returns it, since the FIFO is consumed as it is written.
void Bw2RecordDrawSyncToken(unsigned int token);

