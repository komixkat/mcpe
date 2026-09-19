#include "presence.h"

#include "discord_ipc.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
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
constexpr const char* kJoinPath = "/data/data/com.mojang.minecraftpe/discordrpc.join";
constexpr const char* kDebugPath = "/data/data/com.mojang.minecraftpe/discordrpc.debug";
constexpr const char* kVersionsPath =
    "/data/data/com.mojang.minecraftpe/versions/versions.ini";

properties::property_list kConf('=');
properties::property<std::string> kClientId(kConf, "client_id", "");
properties::property<bool> kLogEnabled(kConf, "log", true);
properties::property<bool> kShowVersion(kConf, "show_version", false);
properties::property<std::string> kLargeImage(kConf, "large_image", "");
properties::property<std::string> kLargeText(kConf, "large_text", "");
properties::property<std::string> kSmallImage(kConf, "small_image", "");
properties::property<std::string> kSmallText(kConf, "small_text", "");
properties::property<bool> kJoinEnabled(kConf, "join_enabled", true);
properties::property<int> kJoinMax(kConf, "join_max", 10);
properties::property<std::string> kJoinAddress(kConf, "join_address", "");
properties::property<std::string> kMultiplayer(kConf, "multiplayer", "");
properties::property<std::string> kServerName(kConf, "server_name", "");
properties::property<std::string> kDimension(kConf, "dimension", "");

std::string gClientId;
std::string gVersion;
bool gLog = true;
bool gShowVersion = false;
std::string gLargeImage, gLargeText, gSmallImage, gSmallText;
bool gJoinEnabled = true;
int gJoinMax = 10;
std::string gJoinAddress;
std::string gMultiplayer;  // "" (auto) | "server" | "realm"
std::string gServerName;   // optional: display name for external servers
std::string gDimensionOverride;  // optional static dimension label (servers/realms)
int gLastDim = -1;         // last auto-detected world dimension (0/1/2/-1)

// ---------------------------------------------------------------------------
// small file/string helpers
// ---------------------------------------------------------------------------

std::string trim(std::string s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

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
            return trim(v);
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
// followed by an NBT compound holding LevelName (TAG_String) and GameType
// (TAG_Int). Parse defensively.
// ---------------------------------------------------------------------------

namespace nbt {

struct Reader {
    const std::vector<unsigned char>& d;
    size_t pos = 0;
    int depth = 0;
    bool fail = false;
    std::string levelName;
    int gameType = -1;

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
            if (tag == 3 && name == "GameType") {  // TAG_Int
                gameType = i32();
            } else if (tag == 8 && name == "LevelName") {  // TAG_String
                levelName = str();
            } else {
                skipPayload(tag);
            }
            if (!levelName.empty() && gameType >= 0) {
                --depth;
                return;  // both fields found
            }
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

// Returns (display name, game type) from a world's level.dat.
std::pair<std::string, int> readLevelData(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {"", -1};
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (data.size() < 9) return {"", -1};
    // Level.dat has an 8-byte header; pure NBT dumps start directly with 0x0A.
    size_t start = (data[0] == 0x0a && data[1] == 0 && data[2] == 0 && data[3] == 0) ? 8 : 0;
    if (data[start] != 0x0a) return {"", -1};
    nbt::Reader r(data);
    r.pos = start + 1;  // skip root TAG_Compound id
    r.compound();
    return {trim(r.levelName), r.gameType};
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

// ---------------------------------------------------------------------------
// multiplayer posture: what is this process actually connected to?
//
// NOTE: detection is intentionally config-driven ("multiplayer=server" or
// "multiplayer=realm" in discordrpc.conf). Auto-detection from /proc/net is
// NOT reliable in this fork: the launcher proxies every game connection
// through its own internal NAT (10.10.x.x / 192.168.x.x), so singleplayer
// sessions look identical to server/realm sessions socket-wise. The scan
// below is kept only to feed discordrpc.debug so a real server/realm session
// can be fingerprinted (open files + sockets) for future exact detection.
// ---------------------------------------------------------------------------

namespace {

// IPv4 addresses in /proc/net are little-endian hex: the LAST two chars are
// the first octet. 0100007F -> 127.0.0.1. Flag loopback/unspecified/multicast.
bool isIgnorableIpv4(const std::string& h /* 8 hex chars */) {
    if (h.size() != 8) return true;
    unsigned char last = static_cast<unsigned char>(
        std::strtol(h.substr(6, 2).c_str(), nullptr, 16));
    return last == 0x00 || last == 0x7f || (last >= 0xe0 && last <= 0xef);
}

// True for IPv6 (32 hex chars): unspecified/loopback/multicast/link-local.
bool isIgnorableIpv6(const std::string& h) {
    if (h.size() != 32) return true;
    std::string a = h.substr(0, 2);
    unsigned char first = static_cast<unsigned char>(
        std::strtol(a.c_str(), nullptr, 16));
    if (first == 0x00) {  // ::, ::1
        for (char c : h) {
            if (c != '0') return false;
        }
        return true;  // all zero (::)
    }
    if (first == 0xfe && (h[2] == '8' || h[2] == '9' || h[2] == 'a' || h[2] == 'b'))
        return true;  // link-local fe80::/10
    if (first == 0xff) return true;  // multicast
    return false;
}

}  // namespace

struct NetScan {
    bool anyRemote = false;       // any established remote (incl. telemetry)
    std::string endpoints;        // compact list for the debug dump
};

NetScan scanNet() {
    NetScan out;
    const char* files[] = {"/proc/net/tcp", "/proc/net/tcp6",
                           "/proc/net/udp", "/proc/net/udp6"};
    for (const char* f : files) {
        bool v6 = std::strstr(f, "6") != nullptr;
        std::ifstream in(f);
        if (!in) continue;
        std::string line;
        std::getline(in, line);  // header
        while (std::getline(in, line)) {
            std::istringstream ss(line);
            std::string sl, local, rem, st;
            if (!(ss >> sl >> local >> rem >> st)) continue;
            if (st != "01") continue;  // established / connected UDP only

            size_t rc = rem.rfind(':');
            if (rc == std::string::npos) continue;
            std::string raddrHex = rem.substr(0, rc);
            std::string rportHex = rem.substr(rc + 1);
            if (rportHex.empty()) continue;

            bool ignore = v6 ? isIgnorableIpv6(raddrHex) : isIgnorableIpv4(raddrHex);
            if (ignore) continue;

            out.anyRemote = true;
            if (!out.endpoints.empty()) out.endpoints += ", ";
            out.endpoints += std::string(v6 ? "[v6]" : "[v4]") + raddrHex + ":" +
                             rportHex + (std::strstr(f, "udp") ? "(u)" : "(t)");
        }
    }
    return out;
}

// Open game-related file paths, for the debug dump / realm fingerprint.
std::string openDataFds() {
    DIR* d = opendir("/proc/self/fd");
    if (!d) return "";
    std::string out;
    char buf[PATH_MAX];
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (!std::isdigit(static_cast<unsigned char>(e->d_name[0]))) continue;
        std::string link = std::string("/proc/self/fd/") + e->d_name;
        ssize_t n = readlink(link.c_str(), buf, sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = '\0';
        if (!std::strstr(buf, "com.mojang") && !std::strstr(buf, "minecraftWorlds") &&
            !std::strstr(buf, "/minecraftpe/"))
            continue;
        if (std::strstr(buf, "/db/") || std::strstr(buf, "level.dat") ||
            std::strstr(buf, "minecraftWorlds") || std::strstr(buf, "/logs/") ||
            std::strstr(buf, "telemetry") || std::strstr(buf, "catalog") ||
            std::strstr(buf, "blob_cache")) {
            if (!out.empty()) out += ", ";
            out += buf;
        }
    }
    closedir(d);
    return out;
}

// ---------------------------------------------------------------------------
// state construction
// ---------------------------------------------------------------------------

enum class WorldMode { Survival, Creative, Adventure, Spectator, Unknown };

WorldMode modeOf(int gameType) {
    switch (gameType) {
        case 0: return WorldMode::Survival;
        case 1: return WorldMode::Creative;
        case 2: return WorldMode::Adventure;
        case 6: return WorldMode::Spectator;
        default: return WorldMode::Unknown;
    }
}

std::string worldStateText(const std::string& name, int gameType) {
    switch (modeOf(gameType)) {
        case WorldMode::Survival: return "In a survival world: " + name;
        case WorldMode::Creative: return "In a creative world: " + name;
        case WorldMode::Adventure: return "In an adventure world: " + name;
        case WorldMode::Spectator: return "Spectating: " + name;
        default: return "In a world: " + name;
    }
}

struct WorldInfo {
    std::string dir;
    std::string id;
    std::string name;
    int gameType = -1;
};

// ---------------------------------------------------------------------------
// dimension detection (hook-free, validated on 1.26.51.1)
//
// Bedrock keeps a single key in its world leveldb equal to the CURRENT
// dimension's name ("Overworld", "Nether", "TheEnd") - when the player
// changes dimension the game writes the new marker. Live
// writes land in the newest <db>/NNNNNNN.log, so we scan that file (with a
// size cap) for a key-form occurrence of one of the three names:
//   [entry-type][keylen==len(name)]name[value-length varint]
// Content occurrences (e.g. "minecraft:nether_*" strings) do not match the
// key shape, and only the active dimension's marker is present at a time.
// Returns 0=Overworld 1=Nether 2=TheEnd, or -1 when nothing reliable.
// ---------------------------------------------------------------------------
int probeDimension(const std::string& worldDir) {
    static const struct {
        const char* name;
        int dim;
    } markers[] = {{"Overworld", 0}, {"Nether", 1}, {"TheEnd", 2}};

    std::string dbDir = worldDir + "/db";
    DIR* d = opendir(dbDir.c_str());
    if (!d) return -1;
    std::string newest;
    long best = -1;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        const char* dot = std::strrchr(e->d_name, '.');
        if (!dot || std::strcmp(dot, ".log") != 0) continue;
        char* endp = nullptr;
        long num = std::strtol(e->d_name, &endp, 10);
        if (endp == e->d_name || *endp != '.') continue;
        if (num > best) {
            best = num;
            newest = std::string(e->d_name);
        }
    }
    closedir(d);
    if (newest.empty()) return -1;

    std::string path = dbDir + "/" + newest;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return -1;
    std::streamsize size = in.tellg();
    if (size <= 0 || size > (8 << 20)) return -1;  // cap at 8 MiB
    in.seekg(0);
    std::vector<char> data(static_cast<size_t>(size));
    if (!in.read(data.data(), size)) return -1;

    int found = -1;
    size_t bestPos = 0;
    for (const auto& m : markers) {
        size_t len = std::strlen(m.name);
        if (len > 127) continue;
        size_t pos = 0;
        while ((pos = std::search(data.begin() + pos, data.end(),
                                  m.name, m.name + len) -
                        data.begin()) < data.size()) {
            // key-form check: [entry-type] [keylen==len] name [vallen varint]
            if (pos >= 1 && static_cast<unsigned char>(data[pos - 1]) == len) {
                unsigned char after =
                    pos + len < data.size() ? static_cast<unsigned char>(data[pos + len]) : 0xff;
                if (after != 0xff && after < 0x80 && after > 0 &&
                    !(after >= 0x0a && after <= 0x1f)) {  // not a tag-name length prefix
                    if (pos > bestPos) {
                        bestPos = pos;
                        found = m.dim;
                    }
                }
            }
            pos += len;
        }
    }
    return found;
}

std::string dimensionText(int dim) {
    switch (dim) {
        case 0: return "Overworld";
        case 1: return "Nether";
        case 2: return "The End";
        default: return "Unknown";
    }
}

int dimensionFromOverride(const std::string& value) {
    std::string v = trim(value);
    if (v == "0" || v == "overworld" || v == "Overworld") return 0;
    if (v == "1" || v == "nether" || v == "Nether") return 1;
    if (v == "2" || v == "end" || v == "the_end" || v == "The End") return 2;
    return -1;
}

WorldInfo probeWorld() {
    WorldInfo w;
    w.dir = findOpenWorldDir();
    if (w.dir.empty()) return w;
    size_t pos = w.dir.rfind('/');
    w.id = pos == std::string::npos ? w.dir : w.dir.substr(pos + 1);
    std::string display = readFirstLine(w.dir + "/levelname.txt");
    auto ld = readLevelData(w.dir + "/level.dat");
    if (display.empty()) display = ld.first;
    if (display.empty()) display = w.id;
    w.name = display;
    w.gameType = ld.second;
    return w;
}

// True when this process holds a blob_cache fd open. While a server/realm
// session is live the game streams chunks into minecraftpe/blob_cache/ and
// keeps its write-ahead log fd open for the whole session; when the session
// ends the fd is closed. This mirrors findOpenWorldDir(): exactly one of the
// two is ever active.
bool blobCacheOpen() {
    DIR* d = opendir("/proc/self/fd");
    if (!d) return false;
    bool open = false;
    char buf[PATH_MAX];
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (!std::isdigit(static_cast<unsigned char>(e->d_name[0]))) continue;
        std::string link = std::string("/proc/self/fd/") + e->d_name;
        ssize_t n = readlink(link.c_str(), buf, sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = '\0';
        if (std::strstr(buf, "/minecraftpe/blob_cache/")) {
            open = true;
            break;
        }
    }
    closedir(d);
    return open;
}

// True when the player is on a server or Realm: no local world open AND the
// process is streaming chunks into its blob_cache (see above).
bool multiplayerOnline() {
    return blobCacheOpen();
}

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool sameActivity(const Activity& x, const Activity& y) {
    return x.state == y.state && x.details == y.details && x.startMs == y.startMs &&
           x.partyId == y.partyId && x.partySize == y.partySize &&
           x.partyMax == y.partyMax && x.joinSecret == y.joinSecret;
}

std::string assetsSummary() {
    std::string s;
    if (!gLargeImage.empty()) s += "large:\"" + gLargeImage + "\"";
    if (!gSmallImage.empty()) s += (s.empty() ? "" : ",") + std::string("small:\"") +
                                  gSmallImage + "\"";
    if (gLargeText.empty() && gSmallText.empty() && s.empty()) s = "(none configured)";
    return s;
}

void writeStateFile(const DiscordIpc& ipc, const Activity& a,
                    const WorldInfo& world, const std::string& multiplayer) {
    std::ofstream out(kStatePath, std::ios::trunc);
    if (!out) return;
    out << "connected=" << (ipc.connected() ? "1" : "0") << "\n";
    out << "state=" << a.state << "\n";
    out << "details=" << a.details << "\n";
    out << "world=" << (world.dir.empty() ? std::string("(none)") : world.name) << "\n";
    out << "dimension=" << (gLastDim >= 0 ? dimensionText(gLastDim)
                                          : std::string("(auto)")) << "\n";
    out << "multiplayer=" << multiplayer << "\n";
    if (a.partyMax > 0)
        out << "party=" << a.partySize << "/" << a.partyMax << "\n";
    out << "join=" << (a.joinSecret.empty() ? std::string("") : std::string("ready"))
        << "\n";
    out << "assets=" << assetsSummary() << "\n";
    out << "version=" << gVersion << "\n";
    out << "error=" << ipc.lastError() << "\n";
}

// Debug-dump helpers; definitions live in the detection block below.
std::string debugServerAuto();
std::string debugServerKind();
std::string debugResolvedHosts();
std::string debugCatalogState();

void writeJoinFile(const JoinRequest& r, const std::string& joinAddress) {
    std::ofstream out(kJoinPath, std::ios::trunc);
    if (!out) return;
    out << "username=" << r.username << "\n";
    out << "user_id=" << r.userId << "\n";
    out << "secret=" << r.secret << "\n";
    out << "connect=" << (joinAddress.empty()
                              ? std::string("ask the host for their LAN/VPN address")
                              : joinAddress)
        << "\n";
}

void writeDebugFile(const WorldInfo& world, const NetScan& net,
                    const std::string& label) {
    std::ofstream out(kDebugPath, std::ios::trunc);
    if (!out) return;
    out << "ts=" << nowMs() << "\n";
    out << "world_dir=" << (world.dir.empty() ? std::string("(none)") : world.dir) << "\n";
    out << "world_name=" << (world.dir.empty() ? std::string("(none)") : world.name)
        << "\n";
    out << "game_type=" << world.gameType << "\n";
    out << "dimension=" << (gLastDim >= 0 ? dimensionText(gLastDim)
                                           : std::string("(unknown)")) << "\n";
    out << "blob_cache_open=" << (blobCacheOpen() ? "1" : "0") << "\n";
    out << "online=" << (multiplayerOnline() ? "1" : "0") << "\n";
    out << "conf_multiplayer=" << (gMultiplayer.empty() ? std::string("(auto)") : gMultiplayer)
        << "\n";
    out << "sockets=" << (net.endpoints.empty() ? std::string("(none)") : net.endpoints)
        << "\n";
    out << "data_fds=" << openDataFds() << "\n";
    out << "server_auto=" << debugServerAuto() << "\n";
    out << "server_kind=" << debugServerKind() << "\n";
    out << "resolved=" << debugResolvedHosts() << "\n";
    out << "catalog=" << debugCatalogState() << "\n";
    out << "label=" << label << "\n";
}

// ---------------------------------------------------------------------------
// server & realm auto-detection (reads what the game actually connects to)
//
// The launcher's libc shim records every hostname the game resolves into
// resolved_hosts.log in the same data dir. When we are online without a local
// world (third-party server / realm), we look at the most recent hostnames
// and:
//   - match them against the featured-server catalog (on disk at
//     ContentCache/ThirdPartyServer/ExperienceManifest) -> server name
//   - pocket.realms.* -> Realm
// This only names what we can actually observe; anything else (no recorder
// yet, opaque relay hosts, ...) falls back to the generic label and config
// overrides. All parsing is defensive and tolerate failures.
// ---------------------------------------------------------------------------
constexpr const char* kResolvedHostsPath =
    "/data/data/com.mojang.minecraftpe/resolved_hosts.log";
constexpr const char* kCatalogDir =
    "/data/data/com.mojang.minecraftpe/minecraftpe/ContentCache/ThirdPartyServer/"
    "ExperienceManifest";
constexpr long long kResolvedHostsWindowSec = 300;    // consider last 5 min
constexpr long long kHostsReadEveryMs = 3000;         // reread log at most every 3 s
constexpr long long kCatalogReloadSec = 60;           // refresh catalog every 60 s

struct CatalogEntry {
    std::string name;
    std::string domain;  // base domain, e.g. "cubecraft.net"
    std::string full;    // exact host, e.g. "play.cubecraft.net"
};

struct ServerDetect {
    std::string name;  // resolved server display name ("" when none)
    std::string kind;  // "server", "realm" or ""
    std::string hosts; // debug only: recent hostnames considered
};

std::string lowerAscii(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in)
        out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    return out;
}

// Reads a JSON string starting at `open` (the opening quote). Returns the raw
// content (escape pairs preserved) and sets `end` to the closing quote index.
std::string readJsonStringAt(const std::string& s, size_t open, size_t& end) {
    std::string out;
    size_t i = open + 1;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            out += s[i];
            out += s[i + 1];
            i += 2;
        } else {
            out += s[i++];
        }
    }
    end = i;
    return out;
}

// Pull (name, hosts) out of a featured-server catalog manifest. The payload
// is a JSON array of catalog items; each 3PP item carries a
// "Title":{"NEUTRAL":...} plus DisplayProperties "url"/"whitelistUrl"/
// "allowListUrl" hosts. Everything here is best-effort: a schema change just
// yields a smaller/empty catalog and the RPC falls back to generic labels.
void parseCatalogFile(const std::string& data, std::vector<CatalogEntry>& out) {
    const std::string itemSep = "\"ContentType\":\"3PP_V2.0\"";
    const std::string titleKey = "\"Title\":{\"NEUTRAL\":\"";
    const std::string titleKeyLow = "\"Title\":{\"neutral\":\"";
    const char* urlKeys[] = {"\"url\":\"", "\"whitelistUrl\":\"",
                             "\"allowListUrl\":\""};

    std::vector<std::string> hosts;  // reused per item
    size_t pos = 0;
    while ((pos = data.find(itemSep, pos)) != std::string::npos) {
        const size_t itemStart = pos;
        pos += itemSep.size();
        size_t itemEnd = data.find(itemSep, pos);
        if (itemEnd == std::string::npos) itemEnd = data.size();

        std::string name;
        size_t t = data.find(titleKey, itemStart);
        if (t != std::string::npos && t < itemEnd) {
            size_t e = 0;
            name = readJsonStringAt(data, t + titleKey.size() - 1, e);
        } else {
            t = data.find(titleKeyLow, itemStart);
            if (t != std::string::npos && t < itemEnd) {
                size_t e = 0;
                name = readJsonStringAt(data, t + titleKeyLow.size() - 1, e);
            }
        }
        name = trim(name);
        if (name.empty()) continue;

        hosts.clear();
        std::string exact;
        std::string domain;
        for (const char* key : urlKeys) {
            const size_t keyLen = std::strlen(key);
            size_t p = itemStart;
            while (p < itemEnd &&
                   (p = data.find(key, p)) != std::string::npos && p < itemEnd) {
                size_t e = 0;
                std::string v =
                    trim(readJsonStringAt(data, p + keyLen - 1, e));
                p += keyLen;
                if (v.empty()) continue;
                // keep only the host part (strip scheme / path)
                const size_t sl = v.find("://");
                const size_t hostStart = (sl == std::string::npos) ? 0 : sl + 3;
                size_t hostEnd = v.find('/', hostStart);
                if (hostEnd == std::string::npos) hostEnd = v.size();
                std::string host =
                    lowerAscii(v.substr(hostStart, hostEnd - hostStart));
                const size_t colon = host.rfind(':');
                if (colon != std::string::npos) host = host.substr(0, colon);
                if (host.empty() || host.find('.') == std::string::npos) continue;
                if (host.size() >= 2 && host[0] == '*' && host[1] == '.')
                    host = host.substr(2);  // allow-list wildcard
                if (exact.empty()) exact = host;

                // base domain = last two labels
                std::string d = host;
                size_t dot = d.find('.');
                if (dot != std::string::npos) {
                    d = d.substr(dot + 1);
                    if (d.find('.') == std::string::npos) d = host;
                }
                if (domain.empty() || host == exact) domain = d;
                hosts.push_back(host);
            }
        }
        if (!exact.empty() && !domain.empty())
            out.push_back({name, domain, exact});
    }
}

void loadServerCatalog(std::vector<CatalogEntry>& out) {
    DIR* d = opendir(kCatalogDir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        const std::string fn = e->d_name;
        if (fn == "." || fn == "..") continue;
        const std::string path = std::string(kCatalogDir) + "/" + fn;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || st.st_size <= 0 ||
            st.st_size > (8 << 20))
            continue;
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        std::string data((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        parseCatalogFile(data, out);
    }
    closedir(d);
}

// Read the tail of resolved_hosts.log; returns (ts, host) pairs within the
// window, newest first. Missing file (old client without the recorder) yields
// an empty list which just means "no auto-detection".
std::vector<std::pair<long long, std::string>> readResolvedHosts() {
    std::vector<std::pair<long long, std::string>> out;
    std::ifstream in(kResolvedHostsPath, std::ios::binary | std::ios::ate);
    if (!in) return out;
    const std::streamoff size = in.tellg();
    if (size <= 0) return out;
    const std::streamoff tailCap = 64 * 1024;
    in.seekg(size > tailCap ? size - tailCap : 0);
    const long long now = nowMs() / 1000;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        long long ts = 0;
        std::string host;
        if (!(ss >> ts >> host)) continue;
        if (host.empty() || now - ts > kResolvedHostsWindowSec) continue;
        out.emplace_back(ts, host);
    }
    std::reverse(out.begin(), out.end());  // newest first
    return out;
}

// Classifies the current session from recent resolved hostnames. Catalog
// matches win (a real server), then Realm markers; else nothing detected.
//
// Discriminator: the game pings every featured host once per server-list
// refresh, but the server the player is actually on gets re-resolved (join +
// connection upkeep). So we prefer hostnames that were resolved at least
// twice within the window, newest first, and fall back to the newest single
// resolution when nothing repeats yet.
void detectCurrentServer(const std::vector<CatalogEntry>& catalog,
                         const std::vector<std::pair<long long, std::string>>& hosts,
                         ServerDetect& out) {
    out = ServerDetect{};
    if (hosts.empty()) return;

    // tally each host: newest resolution timestamp + occurrence count
    struct Tally {
        std::string host;
        long long lastTs;
        int count;
    };
    std::vector<Tally> tally;
    for (const auto& hp : hosts) {
        std::string h = lowerAscii(hp.second);
        if (!h.empty() && h.back() == '.') h.pop_back();
        if (h.empty()) continue;
        bool found = false;
        for (auto& t : tally) {
            if (t.host == h) {
                if (hp.first > t.lastTs) t.lastTs = hp.first;
                ++t.count;
                found = true;
                break;
            }
        }
        if (!found) tally.push_back({h, hp.first, 1});
    }

    // debug list, newest first, with repeat counts so the calibration data
    // shows which host the game keeps re-resolving (the active server).
    const auto newestFirst = [](const Tally& a, const Tally& b) { return a.lastTs > b.lastTs; };
    std::sort(tally.begin(), tally.end(), newestFirst);
    std::string seen;
    for (const auto& t : tally) {
        if (!seen.empty()) seen += ", ";
        seen += t.host;
        if (t.count > 1) seen += "(x" + std::to_string(t.count) + ")";
    }

    // The client pings every featured host once per server-list refresh, but
    // re-resolves the server it is actually on (join + connection upkeep).
    // Pass 0: hosts resolved 2+ times (the active server). Pass 1: everything
    // else (fresh join where only a single resolution has happened yet).
    // A catalog match always wins over the Realm marker.
    Tally realmPick;
    for (int pass = 0; pass < 2 && out.name.empty(); ++pass) {
        std::vector<Tally> cands;
        for (const auto& t : tally)
            if ((pass == 0 && t.count > 1) || (pass == 1 && t.count == 1)) cands.push_back(t);
        if (cands.empty()) continue;
        std::sort(cands.begin(), cands.end(), newestFirst);
        for (const auto& t : cands) {
            if (t.host.find("pocket.realms") != std::string::npos) {
                if (realmPick.host.empty()) realmPick = t;
                continue;
            }
            for (const auto& ce : catalog) {
                const size_t ds = ce.domain.size();
                if (t.host == ce.full ||
                    (t.host.size() > ds &&
                     t.host.compare(t.host.size() - ds, ds, ce.domain) == 0 &&
                     t.host[t.host.size() - ds - 1] == '.')) {
                    out.name = ce.name;
                    out.kind = "server";
                    break;
                }
            }
            if (!out.name.empty()) break;
        }
    }
    if (out.name.empty() && !realmPick.host.empty()) out.kind = "realm";
    out.hosts = seen;
}

// Cached copies + refresh cadence for the catalog and host list.
std::vector<CatalogEntry> gCatalog;
long long gCatalogLoadedAt = 0;
std::vector<std::pair<long long, std::string>> gLastHosts;
long long gHostsReadAt = 0;

void detectServer(ServerDetect& out) {
    const long long now = nowMs();
    if (now - gCatalogLoadedAt > kCatalogReloadSec * 1000) {
        gCatalog.clear();
        loadServerCatalog(gCatalog);
        gCatalogLoadedAt = now;
        // The game rewrites the manifest while we may be mid-read (or it is
        // still downloading); a transient empty load must not stick for a full
        // refresh interval - retry within a few seconds instead.
        if (gCatalog.empty())
            gCatalogLoadedAt -= (kCatalogReloadSec - 5) * 1000;
    }
    if (now - gHostsReadAt > kHostsReadEveryMs) {
        gLastHosts = readResolvedHosts();
        gHostsReadAt = now;
    }
    detectCurrentServer(gCatalog, gLastHosts, out);
}

std::string debugServerAuto() {
    ServerDetect det;
    detectServer(det);
    return det.name.empty() ? std::string("(none)") : det.name;
}

std::string debugServerKind() {
    ServerDetect det;
    detectServer(det);
    return det.kind.empty() ? std::string("(none)") : det.kind;
}

std::string debugResolvedHosts() {
    ServerDetect det;
    detectServer(det);
    return det.hosts.empty() ? std::string("(none)") : det.hosts;
}

std::string debugCatalogState() {
    std::ostringstream out;
    out << "dir=" << kCatalogDir << " entries=" << gCatalog.size();
    for (size_t i = 0; i < gCatalog.size() && i < 3; ++i)
        out << " [" << gCatalog[i].name << "@" << gCatalog[i].domain << "]";
    if (gCatalog.size() > 3) out << " ...";
    return out.str();
}

}  // namespace

// Reloads all config values from discordrpc.conf (applied live every few
// seconds by runPresence). client_id is intentional: changing it mid-session
// would drop the Discord connection, so it stays fixed.
void reloadConfig() {
    try {
        std::ifstream f(kConfPath);
        if (f) kConf.load(f);
        gLog = kLogEnabled.get();
        gShowVersion = kShowVersion.get();
        gLargeImage = trim(std::string(kLargeImage.get()));
        gLargeText = trim(std::string(kLargeText.get()));
        gSmallImage = trim(std::string(kSmallImage.get()));
        gSmallText = trim(std::string(kSmallText.get()));
        gJoinEnabled = kJoinEnabled.get();
        gJoinMax = std::max(1, kJoinMax.get());
        gJoinAddress = trim(std::string(kJoinAddress.get()));
        gMultiplayer = trim(std::string(kMultiplayer.get()));
        if (gMultiplayer != "server" && gMultiplayer != "realm") gMultiplayer.clear();
        gServerName = trim(std::string(kServerName.get()));
        gDimensionOverride = trim(std::string(kDimension.get()));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[DiscordRPC] ignoring bad config: %s\n", ex.what());
    }
}

bool presenceInit() {
    try {
        std::ifstream f(kConfPath);
        if (f) kConf.load(f);
        gClientId = trim(std::string(kClientId.get()));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[DiscordRPC] ignoring bad config: %s\n", ex.what());
    }
    reloadConfig();

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
    auto lastConfReload = std::chrono::steady_clock::now();
    auto bootStart = std::chrono::steady_clock::now();
    auto lastDebug = std::chrono::steady_clock::now();
    auto lastDimProbe = std::chrono::steady_clock::time_point::min();

    std::string details = gShowVersion ? "Playing Minecraft " + gVersion : "Playing Minecraft";

    WorldInfo world;
    int pendingJoins = 0;
    std::string stateText = "In the launcher...";
    std::string currentStateDetails = details;
    std::string multiplayerMode;
    std::int64_t startMs = nowMs();
    Activity sent;  // last activity that Discord accepted

    for (;;) {
        try {
            // live config: pick up discordrpc.conf edits within ~15 s
            if (std::chrono::steady_clock::now() - lastConfReload >=
                std::chrono::seconds(15)) {
                lastConfReload = std::chrono::steady_clock::now();
                reloadConfig();
            }

            // ---- sample the game state (always, even while Discord is down) ----
            WorldInfo cur = probeWorld();
            if (cur.dir != world.dir) {  // world opened, changed or closed
                world = cur;
                pendingJoins = 0;
                gLastDim = -1;
            }
            if (!world.dir.empty() &&
                (cur.dir != world.dir ||
                 std::chrono::steady_clock::now() - lastDimProbe >=
                     std::chrono::seconds(5))) {
                lastDimProbe = std::chrono::steady_clock::now();
                gLastDim = probeDimension(world.dir);
            }

            NetScan net = scanNet();
            bool online = multiplayerOnline();  // server/realm chunk streaming
            ServerDetect detected;
            if (online && world.dir.empty()) detectServer(detected);
            multiplayerMode = world.dir.empty()
                                  ? (online
                                         ? (gMultiplayer == "realm" ? "realm"
                                            : !gServerName.empty() ? "server:" + gServerName
                                            : gMultiplayer == "server" ? "server"
                                            : !detected.name.empty()
                                                  ? "server:" + detected.name
                                            : detected.kind == "realm" ? "realm"
                                            : gMultiplayer.empty() ? "server-or-realm"
                                                                   : gMultiplayer)
                                         : (gMultiplayer.empty() ? "none" : gMultiplayer))
                                  : "world";

            double elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - bootStart)
                                 .count();

            std::string nextState;
            std::string currentDetails = details;
            if (!world.dir.empty()) {
                if (gLastDim >= 0) {
                    nextState = "In the " + dimensionText(gLastDim);
                    currentDetails =
                        world.name.empty() ? details : "Playing " + world.name;
                } else {
                    nextState = worldStateText(world.name, world.gameType);
                }
            } else if (online) {
                if (gMultiplayer == "realm") {
                    nextState = "On a Realm";
                } else if (!gServerName.empty()) {
                    nextState = "On " + gServerName;
                } else if (gMultiplayer == "server") {
                    nextState = "On a server";
                } else if (!detected.name.empty()) {
                    nextState = "On " + detected.name;
                } else if (detected.kind == "realm") {
                    nextState = "On a Realm";
                } else {
                    nextState = "On a server or Realm";
                }
                if (!gDimensionOverride.empty()) {
                    int od = dimensionFromOverride(gDimensionOverride);
                    if (od >= 0)
                        currentDetails =
                            details + " · In the " + dimensionText(od);
                }
            } else {
                nextState = (elapsed < 10.0) ? "In the launcher..." : "In the menus";
            }

            if (nextState != stateText || currentDetails != currentStateDetails) {
                stateText = nextState;
                currentStateDetails = currentDetails;
                startMs = nowMs();
            }

            // ---- debug dump (calibration; refreshes even if Discord is down) ----
            auto now = std::chrono::steady_clock::now();
            if (now - lastDebug >= std::chrono::seconds(5)) {
                lastDebug = now;
                writeDebugFile(world, net, stateText);
            }

            if (!ipc.connected()) {
                if (now < nextConnect) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    continue;
                }
                nextConnect = now + std::chrono::seconds(backoffSec);
                if (gLog)
                    std::fprintf(stderr, "[DiscordRPC] connecting to Discord...\n");
                if (ipc.connect(gClientId)) {
                    backoffSec = 2;
                    failLogged = false;
                    if (gLog) std::fprintf(stderr, "[DiscordRPC] connected (READY)\n");
                    sent = Activity{};
                } else {
                    if (gLog && !failLogged) {
                        std::fprintf(stderr, "[DiscordRPC] Discord unavailable: %s\n",
                                     ipc.lastError().c_str());
                        failLogged = true;
                    }
                    backoffSec = std::min(backoffSec * 2, 30);
                    writeStateFile(ipc, sent, world, multiplayerMode);
                    continue;
                }
            }

            // ---- assemble the activity (party + join when a world is open) ----
            Activity a;
            a.state = stateText;
            a.details = currentDetails;
            a.startMs = startMs;
            a.largeImage = gLargeImage;
            a.largeText = gLargeText;
            a.smallImage = gSmallImage;
            a.smallText = gSmallText;
            if (!world.dir.empty() && gJoinEnabled && gJoinMax > 0) {
                a.partyId = "world-" + world.id;
                a.partySize = std::min(1 + pendingJoins, gJoinMax);
                a.partyMax = gJoinMax;
                a.joinSecret = gJoinAddress.empty() ? world.id : gJoinAddress;
            }

            auto tnow = std::chrono::steady_clock::now();
            bool changed = !sameActivity(a, sent);
            bool due = tnow >= nextRefresh;
            if (changed || due) {
                nextRefresh = tnow + std::chrono::seconds(240);
                if (changed && gLog)
                    std::fprintf(stderr, "[DiscordRPC] presence: %s\n", stateText.c_str());
                if (!ipc.setActivity(a)) {
                    ipc.disconnect();
                    nextConnect = std::chrono::steady_clock::now();
                    continue;
                }
                sent = a;
                ipc.pump(250);
                if (gLog && !ipc.lastError().empty())
                    std::fprintf(stderr, "[DiscordRPC] Discord error: %s\n",
                                 ipc.lastError().c_str());
                writeStateFile(ipc, a, world, multiplayerMode);
                continue;
            }

            if (!ipc.pump(500)) {
                if (gLog)
                    std::fprintf(stderr, "[DiscordRPC] connection lost: %s\n",
                                 ipc.lastError().c_str());
                backoffSec = 2;
                nextConnect = std::chrono::steady_clock::now();
                continue;
            }

            // ---- handle Join Game requests (friend clicked our Join button) ----
            if (ipc.lastJoin().valid) {
                const JoinRequest& r = ipc.lastJoin();
                if (gLog)
                    std::fprintf(stderr, "[DiscordRPC] join request: %s (%s)\n",
                                 r.username.c_str(), r.userId.c_str());
                writeJoinFile(r, gJoinAddress);
                if (!world.dir.empty() && r.secret == a.joinSecret) ++pendingJoins;
                ipc.clearJoin();
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