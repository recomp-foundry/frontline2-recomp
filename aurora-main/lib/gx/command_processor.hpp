#pragma once

#include "dolphin/gx/GXEnum.h"
#include "../internal.hpp"

namespace aurora::gx::fifo {

// Reset the CP write-deduplication state whenever the GX shadow state is reinitialized.
void reset_cp_register_cache();

// Process a buffer of GX FIFO commands
uint32_t process(const uint8_t* data, uint32_t size, bool bigEndian);

// Submit already-packed direct vertex bytes against the current GX state.
bool submit_raw_draw(GXPrimitive prim, GXVtxFmt fmt, const uint8_t* vertices, uint16_t vtxCount,
                     uint32_t vertexBytes);

// AURORA_DRAW_TRACE_FRAME=N[,M...]: true while frame N is being recorded. Each draw
// and each EFB copy/clear of that frame is logged with the state it uses.
bool draw_trace_active() noexcept;
void draw_trace_note(const char* what) noexcept;

} // namespace aurora::gx::fifo
