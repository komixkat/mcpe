#include "vibrant_visuals_patch.h"

#include <log.h>
#include <game_window_manager.h>
#include <mcpelauncher/path_helper.h>
#include <base64.h>
#include <nlohmann/json.hpp>

#include <exception>
#include <fstream>
#include <iterator>
#include <string>

// The game ships a device compatibility list at:
//     <gameDir>/assets/assets/tiers.bin
// Despite the ".bin" extension it is a base64 encoded JSON document mapping the
// GL_RENDERER string to a device tier, e.g.
//     { "gpu": { "Adreno (TM) 640": 4, ... } }                       (this version)
//     { "gpu": { "Adreno (TM) 640": { "tier": 4, "hash": ... } } }   (newer)
// "Vibrant Visuals" is only offered for entries with tier >= 4. Because
// mcpelauncher runs the Android build of the game, the list only contains
// mobile GPUs (Adreno, Mali, ...). A desktop GPU reported through Mesa is not
// in the list, the game treats it as unsupported and greys the option out.
//
// Whitelist the renderer we are actually running on at the top tier so the
// setting becomes selectable. This mirrors the community "unlock Vibrant
// Visuals" tool, but is applied automatically on every launch (the operation
// is idempotent) so it also survives game updates, which replace the file.

namespace {

// Reads the device tier out of an entry in either supported encoding.
int tierOf(const nlohmann::json& value) {
    if(value.is_number_integer())
        return value.get<int>();
    if(value.is_object()) {
        auto it = value.find("tier");
        if(it != value.end() && it->is_number_integer())
            return it->get<int>();
    }
    return 0;
}

std::string getGpuRenderer() {
    auto windowManager = GameWindowManager::getManager();
    auto glGetString = (const char* (*)(int)) windowManager->getProcAddrFunc()("glGetString");
    if(!glGetString)
        return {};
    const char* renderer = glGetString(0x1F01 /* GL_RENDERER */);
    if(!renderer || !*renderer)
        return {};
    return renderer;
}

}  // namespace

void VibrantVisualsPatch::apply() {
    std::string renderer = getGpuRenderer();
    if(renderer.empty())
        return;

    std::string path = PathHelper::getGameDir() + "assets/assets/tiers.bin";
    std::ifstream in(path, std::ios::binary);
    if(!in.is_open()) {
        // Older game versions do not have a device tier list at all.
        Log::debug("VibrantVisuals", "No device tier list at %s, nothing to do", path.c_str());
        return;
    }
    std::string encoded((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::string decoded;
    try {
        decoded = Base64::decode(encoded);
    } catch(std::exception& e) {
        Log::warn("VibrantVisuals", "Could not decode %s: %s", path.c_str(), e.what());
        return;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(decoded);
    } catch(std::exception& e) {
        Log::warn("VibrantVisuals", "Could not parse %s: %s", path.c_str(), e.what());
        return;
    }

    auto gpu = root.find("gpu");
    if(gpu == root.end() || !gpu->is_object()) {
        Log::warn("VibrantVisuals", "Unexpected device tier list format in %s", path.c_str());
        return;
    }

    auto existing = gpu->find(renderer);
    if(existing != gpu->end()) {
        if(tierOf(*existing) >= 4) {
            Log::info("VibrantVisuals", "GPU '%s' is already whitelisted (tier %d)", renderer.c_str(), tierOf(*existing));
            return;
        }
        // Present but below the tier that enables Vibrant Visuals; raise it.
        if(existing->is_object())
            (*existing)["tier"] = 5;
        else
            *existing = 5;
    } else {
        // Match the entry encoding the current game version uses, otherwise the
        // game fails to parse the list ("Failed to parse device tiers JSON.").
        bool objectEntries = false;
        for(auto& entry : gpu->items()) {
            if(entry.value().is_object()) {
                objectEntries = true;
                break;
            }
        }
        if(objectEntries)
            (*gpu)[renderer] = {{"tier", 5}, {"hash", 2147483646}};
        else
            (*gpu)[renderer] = 5;
    }

    std::string newEncoded;
    try {
        newEncoded = Base64::encode(root.dump(), true);
    } catch(std::exception& e) {
        Log::warn("VibrantVisuals", "Could not encode device tier list: %s", e.what());
        return;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if(!out.is_open()) {
        Log::warn("VibrantVisuals", "Could not write %s (is the game directory writable?)", path.c_str());
        return;
    }
    out.write(newEncoded.data(), (std::streamsize) newEncoded.size());
    out.close();
    if(!out) {
        Log::warn("VibrantVisuals", "Failed to write %s", path.c_str());
        return;
    }
    Log::info("VibrantVisuals", "Whitelisted GPU '%s' (tier 5) so Vibrant Visuals can be enabled", renderer.c_str());
}
