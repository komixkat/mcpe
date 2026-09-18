#include "presence.h"

#include "discord_ipc.h"

#include <chrono>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <properties/property.h>
#include <properties/property_list.h>

namespace {

// All paths sit below the launcher data root: /data/data/com.mojang.minecraftpe
// is redirected by the client to ~/.local/share/mcpelauncher.
constexpr const char* kConfPath = "/data/data/com.mojang.minecraftpe/discordrpc.conf";
constexpr const char* kStatePath = "/data/data/com.mojang.minecraftpe/discordrpc.state";
constexpr const char* kVersionsPath =
    "/data/data/com.mojang.minecraftpe/versions/versions.ini";

properties::property_list kConf('=');
properties::property<std::string> kClientId(kConf, "client_id", "");
properties::property<bool> kLogEnabled(kConf, "log", true);

std::string gClientId;
std::string gVersion;
bool gLog = true;

// ---------------------------------------------------------------------------
// small file/string helpers
// ---------------------------------------------------------------------------

std::string readFirstLine(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    if (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
    }
    return line;
}

std::string findVersion() {
    std::ifstream in(kVersionsPath);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("versionName=", 0) == 0) {
            std::string v = line.substr(12);
            while (!v.empty() && (v.back() == '\r' || v.back() == '\n'))
                v.pop_back();
            return v;
        }
    }
    return "";
}

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Bedrock level.dat: an 8-byte header (0x0A 00 00 00 + uint32 LE length)
// followed by an NBT compound whose children include TAG_String "LevelName".
// Parse defensively; return "" on any malformed input.
// ---------------------------------------------------------------------------

namespace nbt {

struct Reader {
    const std::vector<unsigned char>& d;
    size_t pos = 0;
    int depth = 0;
    bool fail = false;
    std::string levelName;

    explicit Reader(const std::vector<unsigned char>& data) : d(data) {}

    bool need(size_t n) {
        if (pos + n > d.size()) {
            fail = true;
            return false;
        }
        return true;
    }

    std::uint16_t u16() {
        if (!need(2)) return 0;
        std::uint16_t v = static_cast<std::uint16_t>(d[pos] | (d[pos + 1] << 8));
        pos += 2;
        return v;
    }

    std::int32_t i32() {
        if (!need(4)) return 0;
        std::int32_t v = d[pos] | (d[pos + 1] << 8) | (d[pos + 2] << 16) |
                         (static_cast<std::int32_t>(d[pos + 3]) << 24);
        pos += 4;
        return v;
    }

    std::string str() {
        std::uint16_t n = u16();
        if (fail || !need(n)) return "";
        std::string s(reinterpret_cast<const char*>(&d[pos]), n);
        pos += n;
        return s;
    }

    void compound() {
        if (depth > 32) {
            fail = true;
            return;
        }
        ++depth;
        while (!fail) {
            if (!need(1)) return;
            unsigned char tag = d[pos++];
            if (tag == 0) break;  // TAG_End
            std::string name = str();
            if (fail) return;
            if (tag == 8 && name == "LevelName") {  // TAG_String
                levelName = str();
                return;  // found what we need; stop early
            }
            skipPayload(tag);
        }
        --depth;
    }

    void skipPayload(unsigned char tag) {
        switch (tag) {
            case 1:  // byte
                if (need(1)) pos += 1;
                break;
            case 2:  // short
                if (need(2)) pos += 2;
                break;
            case 3:  // int
                if (need(4)) pos += 4;
                break;
            case 4:  // long
                if (need(8)) pos += 8;
                break;
            case 5:  // float
                if (need(4)) pos += 4;
                break;
            case 6:  // double
                if (need(8)) pos += 8;
                break;
            case 7: {  // byte array
                std::int32_t count = i32();
                if (!fail && count >= 0 && count <= (1 << 24) && need(static_cast<size_t>(count)))
                    pos += static_cast<size_t>(count);
                break;
            }
            case 8:  // string
                (void)str();
                break;
            case 11: {  // int array
                std::int32_t count = i32();
                if (!fail && count >= 0 && count <= (1 << 24) && need(4ull * count))
                    pos += 4ull * static_cast<size_t>(count);
                break;
            }
            case 12: {  // long array
                std::int32_t count = i32();
                if (!fail && count >= 0 && count <= (1 << 24) && need(8ull * count))
                    pos += 8ull * static_cast<size_t>(count);
                break;
            }
            case 9: {  // list: uint32 count, byte item tag, then payloads
                std::int32_t count = i32();
                if (fail || !need(1)) return;
                unsigned char itemTag = d[pos++];
                if (count < 0 || count > (1 << 20)) {
                    fail = true;
                    return;
                }
                if (itemTag == 10) {
                    for (std::int32_t i = 0; i < count && !fail; ++i) compound();
                } else {
                    for (std::int32_t i = 0; i < count && !fail; ++i) skipPayload(itemTag);
                }
                break;
            }
            case 10:  // compound
                compound();
                break;
            default:
                fail = true;
        }
    }
};

}  // namespace nbt

std::string readLevelNameFromLevelDat(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return "";
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (data.size() < 9) return "";
    // Level.dat has an 8-byte header; pure NBT dumps start directly with 0x0A.
    size_t start = (data[0] == 0x0a && data[1] == 0 && data[2] == 0 && data[3] == 0) ? 8 : 0;
    if (data[start] != 0x0a) return "";
    nbt::Reader r(data);
    r.pos = start + 1;  // skip root TAG_Compound id
    r.compound();
    return r.levelName;
}

// ---------------------------------------------------------------------------
// world detection: which minecraftWorlds dir does this process hold open?
// ---------------------------------------------------------------------------

// Returns the real path of the open world dir, or "" when no world is loaded.
std::string findOpenWorldDir() {
    DIR* d = opendir("/proc/self/fd");
    if (!d) return "";
    std::string result;
    char buf[PATH_MAX];
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (!std::isdigit(static_cast<unsigned char>(e->d_name[0]))) continue;
        std::string link = std::string("/proc/self/fd/") + e->d_name;
        ssize_t n = readlink(link.c_str(), buf, sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = '\0';
        const char* marker = strstr(buf, "/minecraftWorlds/");
        if (!marker) continue;
        const char* seg = marker + std::strlen("/minecraftWorlds/");
        const char* end = std::strchr(seg, '/');
        size_t segLen = end ? static_cast<size_t>(end - seg) : std::strlen(seg);
        if (segLen == 0) continue;
        result = end ? std::string(buf, static_cast<size_t>(end - buf))
                     : std::string(buf);
        break;
    }
    closedir(d);
    return result;
}

std::string worldDisplayName(const std::string& worldDir, const std::string& worldId) {
    std::string name = readFirstLine(worldDir + "/levelname.txt");
    if (name.empty()) name = readLevelNameFromLevelDat(worldDir + "/level.dat");
    if (name.empty()) name = worldId;
    return name;
}

enum class GameState { Launching, MainMenu, Playing };

std::string stateTextFor(GameState st, const std::string& worldName) {
    switch (st) {
        case GameState::Launching: return "In the launcher...";
        case GameState::MainMenu: return "In the main menu";
        case GameState::Playing: return "Playing - " + worldName;
    }
    return "In the main menu";
}

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Tiny status file at the data root so presence can be inspected without
// reading the launcher log: connected, state, details, version, last error.
void writeStateFile(const DiscordIpc& ipc, const std::string& stateText,
                    const std::string& details) {
    std::ofstream out(kStatePath, std::ios::trunc);
    if (!out) return;
    out << "connected=" << (ipc.connected() ? "1" : "0") << "\n";
    out << "state=" << stateText << "\n";
    out << "details=" << details << "\n";
    out << "version=" << gVersion << "\n";
    out << "error=" << ipc.lastError() << "\n";
}

}  // namespace

bool presenceInit() {
    try {
        std::ifstream f(kConfPath);
        if (f) kConf.load(f);
        gClientId = std::string(kClientId.get());
        gLog = kLogEnabled.get();
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[DiscordRPC] ignoring bad config: %s\n", ex.what());
    }

    size_t b = gClientId.find_first_not_of(" \t\r\n");
    size_t e = gClientId.find_last_not_of(" \t\r\n");
    gClientId = (b == std::string::npos) ? "" : gClientId.substr(b, e - b + 1);

    if (!allDigits(gClientId)) {
        std::fprintf(stderr,
                     "[DiscordRPC] disabled: client_id in discordrpc.conf must be "
                     "the numeric Client ID of a Discord application (create one at "
                     "https://discord.com/developers/applications)\n");
        return false;
    }

    gVersion = findVersion();
    if (gLog)
        std::fprintf(stderr, "[DiscordRPC] enabled (client_id %s, version %s)\n",
                     gClientId.c_str(), gVersion.empty() ? "unknown" : gVersion.c_str());
    return true;
}

void runPresence() {
    DiscordIpc ipc;
    int backoffSec = 2;
    bool failLogged = false;
    auto nextConnect = std::chrono::steady_clock::now();
    auto nextRefresh = std::chrono::steady_clock::time_point::min();
    auto bootStart = std::chrono::steady_clock::now();

    std::string details =
        gVersion.empty() ? "Minecraft Bedrock" : "Minecraft Bedrock " + gVersion;

    GameState state = GameState::Launching;
    std::string stateText = stateTextFor(state, "");
    std::string worldDir;
    std::string worldName;
    std::int64_t startMs = nowMs();

    for (;;) {
        try {
            if (!ipc.connected()) {
                auto now = std::chrono::steady_clock::now();
                if (now < nextConnect) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    continue;
                }
                nextConnect = now + std::chrono::seconds(backoffSec);
                if (gLog) std::fprintf(stderr, "[DiscordRPC] connecting to Discord...\n");
                if (ipc.connect(gClientId)) {
                    backoffSec = 2;
                    failLogged = false;
                    if (gLog) std::fprintf(stderr, "[DiscordRPC] connected (READY)\n");
                    if (!ipc.setActivity(stateText, details, startMs)) {
                        ipc.disconnect();
                        continue;
                    }
                    ipc.pump(250);
                    writeStateFile(ipc, stateText, details);
                } else {
                    if (gLog && !failLogged) {
                        std::fprintf(stderr, "[DiscordRPC] Discord unavailable: %s\n",
                                     ipc.lastError().c_str());
                        failLogged = true;
                    }
                    backoffSec = std::min(backoffSec * 2, 30);
                    writeStateFile(ipc, stateText, details);
                    continue;
                }
            }

            // ---- sample the game state ----
            std::string cur = findOpenWorldDir();
            if (cur != worldDir) {  // world opened or closed
                worldDir = cur;
                if (!worldDir.empty()) {
                    size_t pos = worldDir.rfind('/');
                    std::string id = pos == std::string::npos ? worldDir
                                                              : worldDir.substr(pos + 1);
                    worldName = worldDisplayName(worldDir, id);
                } else {
                    worldName.clear();
                }
            }

            double elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - bootStart)
                                 .count();
            GameState next = GameState::MainMenu;
            if (!worldDir.empty())
                next = GameState::Playing;
            else if (elapsed < 10.0)
                next = GameState::Launching;

            std::string nextText = stateTextFor(next, worldName);
            if (next != state || nextText != stateText) {
                state = next;
                stateText = nextText;
                startMs = nowMs();
            }

            auto now = std::chrono::steady_clock::now();
            if (now >= nextRefresh) {  // refresh so presence never expires
                nextRefresh = now + std::chrono::seconds(240);
                if (gLog)
                    std::fprintf(stderr, "[DiscordRPC] presence: %s\n", stateText.c_str());
                if (!ipc.setActivity(stateText, details, startMs)) {
                    ipc.disconnect();
                    nextConnect = std::chrono::steady_clock::now();
                    continue;
                }
                ipc.pump(250);
                if (gLog && !ipc.lastError().empty())
                    std::fprintf(stderr, "[DiscordRPC] Discord error: %s\n",
                                 ipc.lastError().c_str());
                writeStateFile(ipc, stateText, details);
                continue;
            }

            if (!ipc.pump(1000)) {
                if (gLog)
                    std::fprintf(stderr, "[DiscordRPC] connection lost: %s\n",
                                 ipc.lastError().c_str());
                backoffSec = 2;
                nextConnect = std::chrono::steady_clock::now();
            }
        } catch (const std::exception& ex) {
            if (gLog) std::fprintf(stderr, "[DiscordRPC] worker error: %s\n", ex.what());
            std::this_thread::sleep_for(std::chrono::seconds(5));
        } catch (...) {
            if (gLog) std::fprintf(stderr, "[DiscordRPC] worker error (unknown)\n");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}