#include "zoom.h"
#include "main.h"
#include "util.h"
#include "conf.h"
#include <cmath>
#include <cstring>
#include <cstdint>
#include <atomic>

// True while the zoom override is active (key held, or the animated
// transition is still running). Exported so other mods (snaplook) can switch
// to first-person while zoomed, since Bedrock's third-person boom collapses
// into the player when the FOV shrinks.
static std::atomic<bool> zoomActive{false};

extern "C" [[gnu::visibility("default")]] bool mcpelauncher_zoom_active() {
    return zoomActive.load(std::memory_order_relaxed);
}

// CameraAPI::tryGetFOV returns a packed value: low 32 bits are the FOV as an
// IEEE-754 float (radians), high 32 bits are a flag. We only need the float.
static inline float packedFovToFloat(unsigned long v) {
    std::uint32_t bits = static_cast<std::uint32_t>(v & 0xffffffffu);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// Dampen look sensitivity by the angular FOV ratio so a zoomed view keeps the
// same on-screen amount of movement per mouse count. Only applies on launcher
// builds that expose the hook (NULL on older builds).
static void applyZoomSensitivity(unsigned long current, unsigned long normal) {
    if(!game_window_set_mouse_relative_scale)
        return;
    float scale = 1.0f;
    float fz = packedFovToFloat(current);
    float fn = packedFovToFloat(normal);
    if(fz > 1e-4f && fn > 1e-4f) {
        float tz = std::tan(fz * 0.5f);
        float tn = std::tan(fn * 0.5f);
        if(tn > 1e-6f)
            scale = tz / tn;
    }
    if(!(scale > 0.0f) || scale > 1.0f)  // only dampen, never amplify; catches NaN
        scale = 1.0f;
    game_window_set_mouse_relative_scale(game_window_get_primary_window(), scale);
}

unsigned long Zoom::CameraAPI_tryGetFOV(void* t) {
    lastClientZoom = CameraAPI_tryGetFOV_orig(t);
    unsigned long ret;
    if(!Conf::animated) {
        ret = zoomKeyDown ? zoomLevel : lastClientZoom;
    } else if(transition.inProgress() || zoomKeyDown) {
        transition.tick();
        unsigned long current = transition.getCurrent();
        ret = (current == 0) ? lastClientZoom : current;
    } else {
        ret = lastClientZoom;
    }
    bool active = Conf::animated ? (zoomKeyDown || transition.inProgress()) : zoomKeyDown;
    zoomActive.store(active, std::memory_order_relaxed);
    applyZoomSensitivity(ret, lastClientZoom);
    return ret;
}

bool Zoom::onMouseScroll(double dy) {
    if(zoomKeyDown && game_window_is_mouse_locked(game_window_get_primary_window())) {
        if(dy > 0) {
            if(zoomLevel > 5310000000) {
                if(Conf::animated) {
                    transition.startTransition(zoomLevel, zoomLevel - 1000000, 100);
                }
                zoomLevel -= 1000000;
            }
        } else if(zoomLevel < 5360000000) {
            if(Conf::animated) {
                transition.startTransition(zoomLevel, zoomLevel + 1000000, 100);
            }
            zoomLevel += 1000000;
        }
        return true;
    }
    return false;
}
void Zoom::onKeyboard(int keyCode, int action) {
    if(keyCode == Conf::zoomKey) {
        unsigned long diff = unsignedDiff(lastClientZoom, zoomLevel);
        switch(action) {
        case 0:
            if(game_window_is_mouse_locked(game_window_get_primary_window()) && !zoomKeyDown) {
                zoomKeyDown = true;
                if(Conf::animated) {
                    transition.startTransition(lastClientZoom, zoomLevel, clamp(100, diff / 150000, 250));
                }
            }
            break;
        case 2:
            if(zoomKeyDown) {
                zoomKeyDown = false;
                zoomActive.store(false, std::memory_order_relaxed);
                if(game_window_set_mouse_relative_scale)  // fall back to normal sensitivity immediately
                    game_window_set_mouse_relative_scale(game_window_get_primary_window(), 1.0f);
                if(Conf::animated) {
                    transition.startTransition(zoomLevel, lastClientZoom, clamp(100, diff / 150000, 250));
                }
            }
        default:
            break;
        }
    }
}
