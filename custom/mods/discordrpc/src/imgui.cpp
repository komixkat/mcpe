#include "imgui.h"
#include "presence.h"
#include "discord_ipc.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>

extern "C" [[gnu::visibility("default")]] void mod_init();
extern "C" [[gnu::visibility("default")]] void showDiscordRPCConfig();

// Static function pointers for menu callbacks
static void (*g_mcpelauncher_show_window)(const char* title, int isModal, void* user, void (*onClose)(void* user), int count, struct control* controls) = nullptr;
static void (*g_mcpelauncher_addmenu)(size_t length, struct MenuEntryABI* entries) = nullptr;
static void (*g_mod_init)() = nullptr;

void showDiscordRPCConfig() {
    void* libmenu = dlopen("libmcpelauncher_menu.so", 0);
    if(!libmenu) return;

    g_mcpelauncher_show_window = (void (*)(const char* title, int isModal, void* user, void (*onClose)(void* user), int count, struct control* controls))dlsym(libmenu, "mcpelauncher_show_window");
    g_mcpelauncher_addmenu = (void (*)(size_t length, struct MenuEntryABI* entries))dlsym(libmenu, "mcpelauncher_addmenu");

    if(!g_mcpelauncher_addmenu || !g_mcpelauncher_show_window) return;

    g_mod_init = mod_init;

    struct MenuEntryABI clientIdEntry;
    clientIdEntry.name = "Set Client ID";
    clientIdEntry.click = [](void* user) {
        struct control window;
        window.type = 3;
        window.data.text.label = (char*)"Enter Discord Application Client ID (numeric):";
        window.data.text.size = 0;
        g_mcpelauncher_show_window("DiscordRPC Client ID", 1, NULL, [](void* user) {}, 1, &window);
    };
    clientIdEntry.selected = [](void* user) -> bool { return false; };
    clientIdEntry.length = 0;

    struct MenuEntryABI largeImageEntry;
    largeImageEntry.name = "Large Image Key";
    largeImageEntry.click = [](void* user) {
        struct control window;
        window.type = 3;
        window.data.text.label = (char*)"Enter Rich Presence Large Image Key:";
        window.data.text.size = 0;
        g_mcpelauncher_show_window("DiscordRPC Large Image", 1, NULL, [](void* user) {}, 1, &window);
    };
    largeImageEntry.selected = [](void* user) -> bool { return false; };
    largeImageEntry.length = 0;

    struct MenuEntryABI largeTextEntry;
    largeTextEntry.name = "Large Image Text";
    largeTextEntry.click = [](void* user) {
        struct control window;
        window.type = 3;
        window.data.text.label = (char*)"Enter hover text for large image:";
        window.data.text.size = 0;
        g_mcpelauncher_show_window("DiscordRPC Large Text", 1, NULL, [](void* user) {}, 1, &window);
    };
    largeTextEntry.selected = [](void* user) -> bool { return false; };
    largeTextEntry.length = 0;

    struct MenuEntryABI button1LabelEntry;
    button1LabelEntry.name = "Button 1 Label";
    button1LabelEntry.click = [](void* user) {
        struct control window;
        window.type = 3;
        window.data.text.label = (char*)"Enter Button 1 label (e.g. 'Join My Server'):";
        window.data.text.size = 0;
        g_mcpelauncher_show_window("DiscordRPC Button 1 Label", 1, NULL, [](void* user) {}, 1, &window);
    };
    button1LabelEntry.selected = [](void* user) -> bool { return false; };
    button1LabelEntry.length = 0;

    struct MenuEntryABI button1UrlEntry;
    button1UrlEntry.name = "Button 1 URL";
    button1UrlEntry.click = [](void* user) {
        struct control window;
        window.type = 3;
        window.data.text.label = (char*)"Enter Button 1 URL (e.g. 'https://discord.gg/myserver'):";
        window.data.text.size = 0;
        g_mcpelauncher_show_window("DiscordRPC Button 1 URL", 1, NULL, [](void* user) {}, 1, &window);
    };
    button1UrlEntry.selected = [](void* user) -> bool { return false; };
    button1UrlEntry.length = 0;

    struct MenuEntryABI button2EnabledEntry;
    button2EnabledEntry.name = "Enable Button 2";
    button2EnabledEntry.click = [](void* user) {
        // Toggle would need state persistence - for now just info
    };
    button2EnabledEntry.selected = [](void* user) -> bool { return false; };
    button2EnabledEntry.length = 0;

    struct MenuEntryABI button2LabelEntry;
    button2LabelEntry.name = "Button 2 Label";
    button2LabelEntry.click = [](void* user) {
        struct control window;
        window.type = 3;
        window.data.text.label = (char*)"Enter Button 2 label (e.g. 'YouTube', 'Twitch'):";
        window.data.text.size = 0;
        g_mcpelauncher_show_window("DiscordRPC Button 2 Label", 1, NULL, [](void* user) {}, 1, &window);
    };
    button2LabelEntry.selected = [](void* user) -> bool { return false; };
    button2LabelEntry.length = 0;

    struct MenuEntryABI button2UrlEntry;
    button2UrlEntry.name = "Button 2 URL";
    button2UrlEntry.click = [](void* user) {
        struct control window;
        window.type = 3;
        window.data.text.label = (char*)"Enter Button 2 URL (e.g. 'https://youtube.com/@yourchannel'):";
        window.data.text.size = 0;
        g_mcpelauncher_show_window("DiscordRPC Button 2 URL", 1, NULL, [](void* user) {}, 1, &window);
    };
    button2UrlEntry.selected = [](void* user) -> bool { return false; };
    button2UrlEntry.length = 0;

    struct MenuEntryABI reloadEntry;
    reloadEntry.name = "Reload Config";
    reloadEntry.click = [](void* user) {
        if(g_mod_init) g_mod_init(); // Re-initialize to reload config
    };
    reloadEntry.selected = [](void* user) -> bool { return false; };
    reloadEntry.length = 0;

    struct MenuEntryABI entry;
    struct MenuEntryABI entries[] = {
        clientIdEntry, largeImageEntry, largeTextEntry,
        button1LabelEntry, button1UrlEntry, button2EnabledEntry,
        button2LabelEntry, button2UrlEntry, reloadEntry
    };
    entry.subentries = entries;
    entry.length = sizeof(entries) / sizeof(struct MenuEntryABI);
    entry.name = "DiscordRPC";
    g_mcpelauncher_addmenu(1, &entry);
}
