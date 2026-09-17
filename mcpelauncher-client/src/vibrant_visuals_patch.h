#pragma once

// Whitelists the GPU the launcher is actually running on in the game's device
// tier list (<gameDir>/assets/assets/tiers.bin) so "Vibrant Visuals" becomes
// selectable on desktop GPUs that Mesa reports. See vibrant_visuals_patch.cpp
// for the details. Safe to call multiple times; a no-op when there is nothing
// to do.
namespace VibrantVisualsPatch {
    void apply();
}
