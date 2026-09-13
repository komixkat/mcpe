#pragma once

#include <cstdint>

// Workaround for Google PairIP DRM protection of libPlayFabMultiplayer.so on
// Android x86_64 builds (deliberately scrambles PLT/.got.plt entries and
// encrypts the .data section). This is launcher-side fixup applied at runtime
// after the library has been loaded as a transitive dependency of
// libminecraftpe.so.
//
// On non-x86_64 architectures this is a no-op stub so the call site can be
// unconditional.

namespace mcpelauncher {

// Apply the PairIP PLT/.data fixups to libPlayFabMultiplayer.so.
//
// Must be called AFTER libminecraftpe.so has been loaded (which transitively
// loads libPlayFabMultiplayer.so) and BEFORE the game's main thread runs.
// If libPlayFabMultiplayer.so is not loaded, this is a no-op.
void apply_pairip_plt_workaround();

// Apply the PairIP PLT/.data fixups to the library mapped at the given base
// address. Used from the linker's pre-constructor hook so the fixups land
// before libPlayFabMultiplayer.so's own DT_INIT / DT_INIT_ARRAY run (its
// PairIP-scrambled .got.plt would otherwise crash during constructor
// execution). Idempotent across repeated calls.
void apply_pairip_plt_workaround_for(uintptr_t base, const char* libname);

} // namespace mcpelauncher
