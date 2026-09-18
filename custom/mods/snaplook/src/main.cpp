#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>
#include "main.h"
#include "snaplook.h"
#include "conf.h"
#include "imgui.h"
#include <cstddef>
#include <span>
#include <libhat.hpp>
#include <link.h>

Snaplook snapLook;

extern "C" void __attribute__((visibility("default"))) mod_preinit() {
}

// Resolve one game-window export. A launcher build that does not provide a
// symbol must disable the mod cleanly rather than crash on a null call.
template <typename T>
static bool resolveGameWindow(void* gw, const char* name, T& target) {
    target = reinterpret_cast<T>(gw ? dlsym(gw, name) : nullptr);
    if(!target) {
        fprintf(stderr, "[Snaplook] missing game-window export '%s'; snaplook disabled\n", name);
        return false;
    }
    return true;
}

extern "C" __attribute__((visibility("default"))) void mod_init() {
    Conf::load();
    initImgui();

    auto gw = dlopen("libmcpelauncher_gamewindow.so", 0);
    if(!resolveGameWindow(gw, "game_window_is_mouse_locked", game_window_is_mouse_locked) ||
       !resolveGameWindow(gw, "game_window_get_primary_window", game_window_get_primary_window) ||
       !resolveGameWindow(gw, "game_window_add_window_creation_callback", game_window_add_window_creation_callback) ||
       !resolveGameWindow(gw, "game_window_add_keyboard_callback", game_window_add_keyboard_callback))
        return;

    game_window_add_window_creation_callback(NULL, [](void* user) {
        snaplookResolveZoomLink();
        game_window_add_keyboard_callback(game_window_get_primary_window(), NULL, [](void* user, int keyCode, int action) -> bool {
            if(Conf::changingKey) {
                if(action == 0) {
                    Conf::snaplookKey = keyCode;
                    showKeyWindow();
                }
                return true;
            }
            snapLook.onKeyboard(keyCode, action);
            return false;
        });
    });

    auto mcLib = dlopen("libminecraftpe.so", 0);
    if(!mcLib) {
        fprintf(stderr, "[Snaplook] libminecraftpe.so is not loaded; snaplook disabled\n");
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
        fprintf(stderr, "[Snaplook] libminecraftpe text/data ranges not found; snaplook disabled\n");
        return;
    }

    // Resolve the VanillaCameraAPI vtable through RTTI, validating every step so
    // a future game update cannot turn a signature miss into a crash.
    auto VanillaCameraAPI_typeinfo_name_res = hat::find_pattern(range1, hat::object_to_signature("16VanillaCameraAPI"));
    if(!VanillaCameraAPI_typeinfo_name_res.has_result()) {
        fprintf(stderr, "[Snaplook] VanillaCameraAPI typeinfo not found; snaplook disabled\n");
        return;
    }
    auto VanillaCameraAPI_typeinfo_name = VanillaCameraAPI_typeinfo_name_res.get();

    auto VanillaCameraAPI_typeinfo_res = hat::find_pattern(range2, hat::object_to_signature(VanillaCameraAPI_typeinfo_name));
    if(!VanillaCameraAPI_typeinfo_res.has_result()) {
        fprintf(stderr, "[Snaplook] VanillaCameraAPI typeinfo reference not found; snaplook disabled\n");
        return;
    }
    auto VanillaCameraAPI_typeinfo = VanillaCameraAPI_typeinfo_res.get() - sizeof(void*);

    auto VanillaCameraAPI_vtable_res = hat::find_pattern(range2, hat::object_to_signature(VanillaCameraAPI_typeinfo));
    if(!VanillaCameraAPI_vtable_res.has_result()) {
        fprintf(stderr, "[Snaplook] VanillaCameraAPI vtable not found; snaplook disabled\n");
        return;
    }
    auto VanillaCameraAPI_vtable = VanillaCameraAPI_vtable_res.get() + sizeof(void*);
    auto VanillaCameraAPI_getPlayerViewPerspectiveOption = reinterpret_cast<int (**)(void*)>(VanillaCameraAPI_vtable) + 7;

    if(!*VanillaCameraAPI_getPlayerViewPerspectiveOption) {
        fprintf(stderr, "[Snaplook] VanillaCameraAPI perspective slot is empty; snaplook disabled\n");
        return;
    }

    VanillaCameraAPI_getPlayerViewPerspectiveOption_orig = *VanillaCameraAPI_getPlayerViewPerspectiveOption;

    *VanillaCameraAPI_getPlayerViewPerspectiveOption = [](void* t) -> int {
        return snapLook.getPerspectiveOption(t);
    };
}
