#include <dlfcn.h>
#include <link.h>
#include <span>
#include <string>
#include <cstdio>
#include "zoom.h"
#include "main.h"
#include "conf.h"
#include "imgui.h"
#include <libhat.hpp>
#include <span>
#include <cstddef>

Zoom zoom;

unsigned long CameraApi_tryGetFOV(void* t) {
    return zoom.CameraAPI_tryGetFOV(t);
}

extern "C" [[gnu::visibility("default")]] void mod_preinit() {}

// Resolve one game-window export. The game-window API is a list of symbols that
// can grow or change between launcher builds, so a missing symbol must disable
// the mod cleanly instead of leaving a null pointer that crashes on first use.
template <typename T>
static bool resolveGameWindow(void* gw, const char* name, T& target) {
    target = reinterpret_cast<T>(gw ? dlsym(gw, name) : nullptr);
    if(!target) {
        fprintf(stderr, "[Zoom] missing game-window export '%s'; zoom disabled\n", name);
        return false;
    }
    return true;
}

extern "C" [[gnu::visibility("default")]] void mod_init() {
    Conf::load();

    auto gw = dlopen("libmcpelauncher_gamewindow.so", 0);
    if(!resolveGameWindow(gw, "game_window_is_mouse_locked", game_window_is_mouse_locked) ||
       !resolveGameWindow(gw, "game_window_get_primary_window", game_window_get_primary_window) ||
       !resolveGameWindow(gw, "game_window_add_window_creation_callback", game_window_add_window_creation_callback) ||
       !resolveGameWindow(gw, "game_window_add_keyboard_callback", game_window_add_keyboard_callback) ||
       !resolveGameWindow(gw, "game_window_add_mouse_scroll_callback", game_window_add_mouse_scroll_callback))
        return;
    // Optional: older launcher builds do not expose the sensitivity hook.
    game_window_set_mouse_relative_scale = (decltype(game_window_set_mouse_relative_scale))dlsym(gw, "game_window_set_mouse_relative_scale");
    fprintf(stderr, "[Zoom] sensitivity-dampening hook: %s\n", game_window_set_mouse_relative_scale ? "available" : "unavailable (older launcher)");

    auto mcLib = dlopen("libminecraftpe.so", 0);
    if(!mcLib) {
        fprintf(stderr, "[Zoom] libminecraftpe.so is not loaded; zoom disabled\n");
        return;
    }

    std::span<std::byte> range1, range2;

    auto callback = [&](const dl_phdr_info& info) {
        if(auto h = dlopen(info.dlpi_name, RTLD_NOLOAD); dlclose(h), h != mcLib)
            return 0;
        range1 = {reinterpret_cast<std::byte*>(info.dlpi_addr + info.dlpi_phdr[1].p_vaddr), info.dlpi_phdr[1].p_memsz};
        range2 = {reinterpret_cast<std::byte*>(info.dlpi_addr + info.dlpi_phdr[2].p_vaddr), info.dlpi_phdr[2].p_memsz};
        return 1;
    };

    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) {
            return (*static_cast<decltype(callback)*>(data))(*info);
        },
        &callback);

    if(range1.empty() || range2.empty()) {
        fprintf(stderr, "[Zoom] libminecraftpe text/data ranges not found; zoom disabled\n");
        return;
    }

    // Resolve the CameraAPI vtable through RTTI. Each step is validated: a game
    // update that renames or restructures the RTTI must not make us dereference
    // a garbage pointer.
    auto CameraAPI_typeinfo_name_res = hat::find_pattern(range1, hat::object_to_signature("9CameraAPI"));
    if(!CameraAPI_typeinfo_name_res.has_result()) {
        fprintf(stderr, "[Zoom] CameraAPI typeinfo not found; zoom disabled\n");
        return;
    }
    auto CameraAPI_typeinfo_name = CameraAPI_typeinfo_name_res.get();

    auto CameraAPI_typeinfo_res = hat::find_pattern(range2, hat::object_to_signature(CameraAPI_typeinfo_name));
    if(!CameraAPI_typeinfo_res.has_result()) {
        fprintf(stderr, "[Zoom] CameraAPI typeinfo reference not found; zoom disabled\n");
        return;
    }
    auto CameraAPI_typeinfo = CameraAPI_typeinfo_res.get() - sizeof(void*);

    auto CameraAPI_vtable_res = hat::find_pattern(range2, hat::object_to_signature(CameraAPI_typeinfo));
    if(!CameraAPI_vtable_res.has_result()) {
        fprintf(stderr, "[Zoom] CameraAPI vtable not found; zoom disabled\n");
        return;
    }
    auto CameraAPI_vtable = CameraAPI_vtable_res.get() + sizeof(void*);
    auto CameraAPI_tryGetFOV = reinterpret_cast<unsigned long (**)(void*)>(CameraAPI_vtable) + 7;

    if(!*CameraAPI_tryGetFOV) {
        fprintf(stderr, "[Zoom] CameraAPI::tryGetFOV slot is empty; zoom disabled\n");
        return;
    }

    CameraAPI_tryGetFOV_orig = *CameraAPI_tryGetFOV;

    *CameraAPI_tryGetFOV = [](void* t) -> unsigned long {
        return zoom.CameraAPI_tryGetFOV(t);
    };

    initImgui();
    game_window_add_window_creation_callback(NULL, [](void* user) {
        game_window_add_keyboard_callback(game_window_get_primary_window(), NULL, [](void* user, int keyCode, int action) -> bool {
            if(Conf::changingKey) {
                if(action == 0) {
                    Conf::zoomKey = keyCode;
                    showKeyWindow();
                }
                return true;
            }
            if(action != 1) {
                zoom.onKeyboard(keyCode, action);
            }
            return false;
        });
        game_window_add_mouse_scroll_callback(game_window_get_primary_window(), NULL, [](void* user, double x, double y, double dx, double dy) -> bool {
            return zoom.onMouseScroll(dy);
        });
    });
}
