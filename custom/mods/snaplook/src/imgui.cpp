#include "util.h"
#include "imgui.h"
#include "conf.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void initImgui() {
    void* libmenu = dlopen("libmcpelauncher_menu.so", 0);
    if(libmenu) {
        mcpelauncher_show_window = (decltype(mcpelauncher_show_window))dlsym(libmenu, "mcpelauncher_show_window");
        mcpelauncher_close_window = (decltype(mcpelauncher_close_window))dlsym(libmenu, "mcpelauncher_close_window");
        mcpelauncher_addmenu = (decltype(mcpelauncher_addmenu))dlsym(libmenu, "mcpelauncher_addmenu");

        struct MenuEntryABI changeSnaplookKey;
        changeSnaplookKey.name = "Change snaplook key";
        changeSnaplookKey.click = [](void* user) {
            showKeyWindow();
            Conf::changingKey = true;
        };
        changeSnaplookKey.selected = [](void* user) -> bool { return false; };
        changeSnaplookKey.length = 0;

        struct MenuEntryABI entry;
        struct MenuEntryABI entries[] = {changeSnaplookKey};
        entry.subentries = entries;
        entry.length = sizeof(entries) / sizeof(struct MenuEntryABI);
        entry.name = "Snaplook";
        mcpelauncher_addmenu(1, &entry);
    }
}

void showKeyWindow() {
    std::string msg = std::string("Push new key (Current key: " + keyToString(Conf::snaplookKey) + ")");
    struct control snaploookKeyWindow;
    snaploookKeyWindow.type = 3;
    snaploookKeyWindow.data.text.label = (char*)msg.c_str();
    snaploookKeyWindow.data.text.size = 0;

    mcpelauncher_show_window("Snaplook key", 1, NULL, [](void* user) { Conf::save(); Conf::changingKey = false; }, 1, &snaploookKeyWindow);
}