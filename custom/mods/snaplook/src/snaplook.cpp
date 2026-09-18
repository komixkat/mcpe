#include "snaplook.h"
#include "main.h"
#include "conf.h"
#include <dlfcn.h>
#include <cstdio>

namespace {
typedef bool (*ZoomActiveFn)();

// The zoom mod exports mcpelauncher_zoom_active(). Resolve it lazily: mods are
// loaded through the custom linker, so try the default namespace first and then
// an explicit handle by module name.
ZoomActiveFn resolveZoomActive() {
    static ZoomActiveFn fn = nullptr;
    static bool tried = false;
    if(!tried) {
        tried = true;
        fn = (ZoomActiveFn)dlsym(RTLD_DEFAULT, "mcpelauncher_zoom_active");
        if(!fn) {
            void* h = dlopen("libmcpelauncherzoom.so", RTLD_NOLOAD | RTLD_LAZY);
            if(!h)
                h = dlopen("libmcpelauncherzoom.so", RTLD_LAZY);
            if(h)
                fn = (ZoomActiveFn)dlsym(h, "mcpelauncher_zoom_active");
        }
        fprintf(stderr, "[Snaplook] zoom-active hook: %s\n", fn ? "available" : "unavailable");
    }
    return fn;
}
}  // namespace

void snaplookResolveZoomLink() {
    resolveZoomActive();
}

int Snaplook::getPerspectiveOption(void* t) {
    int result = inSnaplook ? 1 : VanillaCameraAPI_getPlayerViewPerspectiveOption_orig(t);
    // While zoomed, use first-person: a reduced FOV collapses Bedrock's
    // third-person boom into the player, so first-person shows the blocks ahead.
    if(result != 0) {
        auto zoomActive = resolveZoomActive();
        if(zoomActive && zoomActive())
            return 0;
    }
    return result;
}

void Snaplook::onKeyboard(int keyCode, int action) {
    if(keyCode == Conf::snaplookKey) {
        // Toggle on key press (action 0 = key down), ignore repeats and release.
        if(action == 0) {
            if(!game_window_is_mouse_locked(game_window_get_primary_window())) {
                return;
            }
            inSnaplook = !inSnaplook;
        }
    }
}
