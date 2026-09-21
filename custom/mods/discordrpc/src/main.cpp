#include <cstdio>
#include <thread>

#include "presence.h"

extern "C" [[gnu::visibility("default")]] void showDiscordRPCConfig();

namespace {

void worker() { runPresence(); }

}  // namespace

// Required by the launcher mod loader; this mod has nothing to do pre-init.
extern "C" void __attribute__((visibility("default"))) mod_preinit() {}

extern "C" __attribute__((visibility("default"))) void mod_init() {
    if (!presenceInit()) {
        std::fprintf(stderr,
                     "[DiscordRPC] disabled: set client_id in discordrpc.conf "
                     "(/data/data/com.mojang.minecraftpe/discordrpc.conf)\n");
        return;
    }
    try {
        std::thread(worker).detach();
        std::fprintf(stderr, "[DiscordRPC] started\n");
    } catch (...) {
        std::fprintf(stderr, "[DiscordRPC] could not start worker thread\n");
    }
    // Register in-game config menu
    showDiscordRPCConfig();
}