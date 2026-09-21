#include "imgui.h"
#include "presence.h"
#include "discord_ipc.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

extern "C" [[gnu::visibility("default")]] void mod_init();
extern "C" [[gnu::visibility("default")]] void showDiscordRPCConfig();

extern "C" [[gnu::visibility("default")]] void mod_init();
extern "C" [[gnu::visibility("default")]] void showDiscordRPCConfig();

// Static function pointers for menu callbacks
static void (*g_mcpelauncher_show_window)(const char* title, int isModal, void* user, void (*onClose)(void* user), int count, struct control* controls) = nullptr;
static void (*g_mcpelauncher_addmenu)(size_t length, struct MenuEntryABI* entries) = nullptr;

void saveConfigValue(const char* key, const char* value) {
    std::string path = "/data/data/com.mojang.minecraftpe/discordrpc.conf";
    std::ifstream in(path);
    std::string content;
    if (in) {
        content.assign(std::istreambuf_iterator<char>(in), {});
    }
    in.close();

    std::string keyStr = key;
    keyStr += "=";
    size_t pos = content.find(keyStr);
    if (pos != std::string::npos) {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos) end = content.size();
        content.replace(pos, end - pos, keyStr + value);
    } else {
        if (!content.empty() && content.back() != '\n') content += "\n";
        content += keyStr + value + "\n";
    }

    std::ofstream out(path);
    out << content;
    out.close();
}

void showTextInput(const char* title, const char* key, const char* placeholder) {
    if (!g_mcpelauncher_show_window) return;

    struct control window;
    window.type = 4;  // textinput type
    window.data.textinput.label = (char*)placeholder;
    window.data.textinput.def = "";
    window.data.textinput.placeholder = (char*)placeholder;
    window.data.textinput.user = (void*)key;
    window.data.textinput.onChange = [](void* user, const char* value) {
        saveConfigValue((const char*)user, value);
    };

    g_mcpelauncher_show_window(title, 1, NULL, [](void* user) {}, 1, &window);
}

void showDiscordRPCConfig() {
    void* libmenu = dlopen("libmcpelauncher_menu.so", 0);
    if (!libmenu) return;

    g_mcpelauncher_show_window = (void (*)(const char* title, int isModal, void* user, void (*onClose)(void* user), int count, struct control* controls))dlsym(libmenu, "mcpelauncher_show_window");
    g_mcpelauncher_addmenu = (void (*)(size_t length, struct MenuEntryABI* entries))dlsym(libmenu, "mcpelauncher_addmenu");

    if (!g_mcpelauncher_addmenu || !g_mcpelauncher_show_window) return;

    struct MenuEntryABI clientIdEntry;
    clientIdEntry.name = "Set Client ID";
    clientIdEntry.click = [](void* user) { showTextInput("DiscordRPC Client ID", "client_id", "123456789012345678"); };
    clientIdEntry.selected = [](void* user) -> bool { return false; };
    clientIdEntry.length = 0;

    struct MenuEntryABI largeImageEntry;
    largeImageEntry.name = "Large Image Key";
    largeImageEntry.click = [](void* user) { showTextInput("DiscordRPC Large Image Key", "large_image", "mcpe-logo"); };
    largeImageEntry.selected = [](void* user) -> bool { return false; };
    largeImageEntry.length = 0;

    struct MenuEntryABI largeTextEntry;
    largeTextEntry.name = "Large Image Text";
    largeTextEntry.click = [](void* user) { showTextInput("DiscordRPC Large Image Text", "large_text", "Minecraft"); };
    largeTextEntry.selected = [](void* user) -> bool { return false; };
    largeTextEntry.length = 0;

    struct MenuEntryABI button1LabelEntry;
    button1LabelEntry.name = "Button 1 Label";
    button1LabelEntry.click = [](void* user) { showTextInput("DiscordRPC Button 1 Label", "button1_label", "Join My Server"); };
    button1LabelEntry.selected = [](void* user) -> bool { return false; };
    button1LabelEntry.length = 0;

    struct MenuEntryABI button1UrlEntry;
    button1UrlEntry.name = "Button 1 URL";
    button1UrlEntry.click = [](void* user) { showTextInput("DiscordRPC Button 1 URL", "button1_url", "https://discord.gg/myserver"); };
    button1UrlEntry.selected = [](void* user) -> bool { return false; };
    button1UrlEntry.length = 0;

    struct MenuEntryABI button2EnabledEntry;
    button2EnabledEntry.name = "Enable Button 2";
    button2EnabledEntry.click = [](void* user) { showTextInput("DiscordRPC Enable Button 2 (true/false)", "button2_enabled", "true"); };
    button2EnabledEntry.selected = [](void* user) -> bool { return false; };
    button2EnabledEntry.length = 0;

    struct MenuEntryABI button2LabelEntry;
    button2LabelEntry.name = "Button 2 Label";
    button2LabelEntry.click = [](void* user) { showTextInput("DiscordRPC Button 2 Label", "button2_label", "YouTube"); };
    button2LabelEntry.selected = [](void* user) -> bool { return false; };
    button2LabelEntry.length = 0;

    struct MenuEntryABI button2UrlEntry;
    button2UrlEntry.name = "Button 2 URL";
    button2UrlEntry.click = [](void* user) { showTextInput("DiscordRPC Button 2 URL", "button2_url", "https://youtube.com/@yourchannel"); };
    button2UrlEntry.selected = [](void* user) -> bool { return false; };
    button2UrlEntry.length = 0;

    struct MenuEntryABI reloadEntry;
    reloadEntry.name = "Reload Config";
    reloadEntry.click = [](void* user) {
        // Reload config without restarting the thread
        loadConfig();
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
