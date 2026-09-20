#include "presence.h"

#include "discord_ipc.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

#include <properties/property.h>
#include <properties/property_list.h>

namespace {

constexpr const char* kConfPath = "/data/data/com.mojang.minecraftpe/discordrpc.conf";

// The whole presence. Discord RPC buttons are plain links, so the profile
// "Join" button opens the Minecraft profile page with one click.
constexpr const char* kDetails = "Playing Minecraft";
constexpr const char* kButtonLabel = "Join komixkat";
constexpr const char* kButtonUrl = "https://launch.minecraft.net/profile/komixkat";

properties::property_list kConf('=');
properties::property<std::string> kClientId(kConf, "client_id", "");
properties::property<bool> kLogEnabled(kConf, "log", true);
properties::property<std::string> kLargeImage(kConf, "large_image", "");
properties::property<std::string> kLargeText(kConf, "large_text", "");

std::string gClientId;
bool gLog = true;
std::string gLargeImage, gLargeText;

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' ||
                     s[e - 1] == '\n'))
        --e;
    return s.substr(b, e - b);
}

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

void loadConfig() {
    try {
        std::ifstream f(kConfPath);
        if (f) kConf.load(f);
        gLog = kLogEnabled.get();
        gLargeImage = trim(std::string(kLargeImage.get()));
        gLargeText = trim(std::string(kLargeText.get()));
    } catch (...) {
    }
}

}  // namespace

bool presenceInit() {
    try {
        std::ifstream f(kConfPath);
        if (f) kConf.load(f);
        gClientId = trim(std::string(kClientId.get()));
    } catch (...) {
    }
    loadConfig();
    // Without a numeric client_id the launcher has nothing to attach the
    // presence / profile button to, so refuse to start.
    return allDigits(gClientId);
}

void runPresence() {
    DiscordIpc ipc;
    const long long startMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    int backoffSec = 2;
    bool failLogged = false;

    while (true) {
        if (!ipc.connected()) {
            if (!ipc.connect(gClientId)) {
                if (gLog && !failLogged) {
                    std::fprintf(stderr, "[DiscordRPC] Discord unavailable: %s\n",
                                 ipc.lastError().c_str());
                    failLogged = true;
                }
                std::this_thread::sleep_for(std::chrono::seconds(backoffSec));
                backoffSec = std::min(backoffSec * 2, 30);
                continue;
            }
            if (gLog) std::fprintf(stderr, "[DiscordRPC] connected\n");
            failLogged = false;
            backoffSec = 2;
        }

        Activity a;
        a.details = kDetails;
        a.startMs = startMs;
        a.largeImage = gLargeImage;
        a.largeText = gLargeText;
        a.buttons = {{kButtonLabel, kButtonUrl}};

        if (!ipc.setActivity(a)) {
            ipc.disconnect();
            continue;
        }
        // Config edits (image keys, log toggle) apply without restarting.
        loadConfig();
        if (!ipc.pump(15000)) ipc.disconnect();
    }
}