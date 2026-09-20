// scan.h - signature-free in-process memory scanner shared by the mod and
// the calibration harness.  The mod runs inside the game's address space, so
// it can read what the game itself keeps in memory: the connected server's
// display name, realm titles, the player's IGN, and dimensions.
//
// Strategy (v3 "whole heap"): instead of guessing that a display name sits
// within a fixed window of the anchor string, harvest human-readable strings
// from the *entire* writable heap (anonymous rw + [heap]) and score them.
// Real names get copied by the game into many heap strings (session objects,
// server-list entries, chat, player lists), so occurrence count is the
// dominant signal; proximity to a known anchor (hostname / XUID) is a soft
// bonus, never a requirement.  Strict filtering keeps false positives out.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// small helpers (kept here, and only here, so the harness tests the exact
// code the mod ships - a previous harness copy of trim() drifted and caused
// a phantom one-char truncation: "CubeCraft" -> "CubeCraf")
// ---------------------------------------------------------------------------
static std::string trim(std::string s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string lowerAscii(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in)
        out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    return out;
}

// ---------------------------------------------------------------------------
// scan constants
// ---------------------------------------------------------------------------
constexpr long long kMemScanEveryMs = 20000;   // at most one scan / 20 s
// Byte budget counts HARVESTED bytes only (chunks we word-parse), not the
// cheap anchor memmem pass; the wall-clock budget below is what really
// bounds a pass on the game thread (the real game heap is ~1.7 GiB).
constexpr size_t kMemScanByteBudget = 512ull << 20;
constexpr long long kScanTimeBudgetMs = 600;   // max ms per pass on the game thread

static long long scanNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
constexpr uintptr_t kProxCapBytes = 8ull << 20;    // proximity bonus reaches
                                                   // this far, then flattens
constexpr size_t kMaxCandidates = 200000;  // dedup map growth cap

struct MemRegion {
    uintptr_t start, end;
};

static std::vector<MemRegion> readMemRegions() {
    std::vector<MemRegion> out;
    // Parse /proc/self/maps with ZERO heap allocations.  Earlier versions used
    // std::ifstream/std::istringstream, whose filebuf + line strings stayed in
    // the process heap and were re-harvested by our own whole-heap scan as
    // path-word junk ("cmD"/"vEe"-class fragments, "usr"/"lib" pairs).  All
    // scratch lives on the stack, which the region filter skips.
    char buf[8192];
    char carry[512];   // at most one (short) line tail across reads
    size_t carryLen = 0;
    int fd = ::open("/proc/self/maps", O_RDONLY);
    if (fd < 0) return out;
    const auto parseLine = [&](const char* s, size_t len) {
        const auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        size_t i = 0;
        uintptr_t start = 0, end = 0;
        while (i < len) { const int v = hexVal(s[i]); if (v < 0) break; start = start * 16 + v; ++i; }
        if (i >= len || s[i] != '-') return;
        ++i;
        while (i < len) { const int v = hexVal(s[i]); if (v < 0) break; end = end * 16 + v; ++i; }
        if (i >= len || s[i] != ' ') return;
        ++i;
        if (i + 4 > len) return;
        const char p0 = s[i], p1 = s[i + 1];  // perms[0] read, perms[1] writable
        i += 4;
        // skip offset, dev, inode
        for (int f = 0; f < 3; ++f) {
            while (i < len && s[i] == ' ') ++i;
            while (i < len && s[i] != ' ') ++i;
        }
        while (i < len && s[i] == ' ') ++i;
        size_t nameStart = i, nameEnd = len;
        while (nameEnd > nameStart && (s[nameEnd - 1] == '\n' || s[nameEnd - 1] == '\r' ||
                                       s[nameEnd - 1] == ' '))
            --nameEnd;
        const size_t nameLen = nameEnd - nameStart;
        if (p0 != 'r') return;  // unreadable mapping
        // only writable anonymous memory + [heap]: that's where server items,
        // profile data and UI strings actually live. Skip stacks, vvar/vdso,
        // code, and huge file-backed mappings (world data, packs). Tiny
        // anonymous rw regions (<256 KiB) are the dynamic loader's private
        // data (link_map path strings like "/usr/lib/.../libstdc++.so.6"),
        // which pollute whole-heap scans with path-word fragments; game heap
        // arenas and world mappings are far larger.
        if (nameLen == 6 && !memcmp(s + nameStart, "[heap]", 6)) {
            out.push_back({start, end});
        } else if (nameLen == 0 && p1 == 'w' &&
                   end - start >= 256 * 1024 && end - start <= (2ull << 30)) {
            out.push_back({start, end});
        }
    };
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
        const size_t got = static_cast<size_t>(n);
        size_t lineStart = 0;
        char lin[1024];
        for (size_t i = 0; i < got; ++i) {
            if (buf[i] == '\n') {
                const size_t seg = i - lineStart;
                size_t tot = carryLen + seg;
                if (tot > sizeof(lin) - 1) tot = sizeof(lin) - 1;
                memcpy(lin, carry, carryLen);
                memcpy(lin + carryLen, buf + lineStart, tot - carryLen);
                parseLine(lin, tot);
                carryLen = 0;
                lineStart = i + 1;
            }
        }
        if (lineStart < got) {
            const size_t rem = got - lineStart;
            if (carryLen + rem < sizeof(carry)) {
                memcpy(carry + carryLen, buf + lineStart, rem);
                carryLen += rem;
            } else {
                carryLen = 0;  // absurdly long line; drop it
            }
        }
    }
    if (carryLen) parseLine(carry, carryLen);
    ::close(fd);
    return out;
}

// Converts a UTF-16LE code-unit run to UTF-8 (basic planes; skips surrogates).
static void appendU16ToUtf8(const std::vector<char>& buf, size_t start,
                            size_t n, std::string& out) {
    for (size_t i = 0; i < n; ++i) {
        const char16_t c = static_cast<unsigned char>(buf[start + i * 2]) |
                           (static_cast<unsigned char>(buf[start + i * 2 + 1])
                            << 8);
        if (c >= 0xD800 && c <= 0xDFFF) continue;  // surrogate halves
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
}

struct ScanHit {
    std::string text;  // UTF-8
    int minDist = 0;   // closest distance to an anchor hit (bytes, capped)
    int count = 0;     // number of heap copies / positions that produced it
};

static std::vector<ScanHit> collectScanHits(const std::string& anchor) {
    std::vector<ScanHit> hits;
    if (anchor.empty()) return hits;
    std::string p16;
    p16.reserve(anchor.size() * 2);
    for (char c : anchor) {
        p16.push_back(c);
        p16.push_back('\0');
    }
    int memFd = ::open("/proc/self/mem", O_RDONLY);
    if (memFd < 0) return hits;

    const size_t chunk = 1u << 20;  // 1 MiB chunks
    size_t budget = kMemScanByteBudget;

    // Snapshot the region list FIRST, then allocate our own buffers.  The
    // 1 MiB scratch buffer and the dedup hash table are big enough that
    // malloc gives them their own anonymous mappings (>= 256 KiB); if those
    // existed when the snapshot was taken, our own candidate strings and the
    // echoed tail of the previous chunk would be harvested back as
    // high-count phantom words ("CmvU"/"5YU"-class).  A bare read of the
    // same regions (probe5) sees none of that noise, so this is entirely
    // self-feedback and must not be part of the scanned set.
    const auto regions = readMemRegions();
    std::vector<char> buf(chunk);
    // dedup: text -> index into hits
    std::unordered_map<std::string, size_t> index;
    index.reserve(1 << 16);

    // Rotating cursor + wall-clock budget: the REAL game heap is ~1.7 GiB of
    // anonymous rw memory, so a whole-heap word harvest on every refresh
    // froze the game for minutes (join timeouts, dead Discord RPC, truncated
    // debug file). Each pass therefore walks only a bounded slice of the heap
    // (pread + cheap memmem for the anchor), and the expensive word harvest
    // runs ONLY on chunks that actually contain an anchor copy (plus the
    // chunk right after one, since a name can straddle the border). The
    // anchor (hostname / XUID) sits in the same session/profile blob as the
    // name, so that is where the decisive evidence lives anyway.
    const size_t nRegions = regions.size();
    static size_t gCursor = 0;  // continuation point across refresh passes
    if (gCursor >= nRegions) gCursor = 0;
    const size_t first = gCursor;
    const long long deadline = scanNowMs() + kScanTimeBudgetMs;
    bool timeUp = false;

    for (size_t ri = 0; ri < nRegions && budget > 0; ++ri) {
        const MemRegion& r = regions[(first + ri) % nRegions];
        uintptr_t pos = r.start;
        const uintptr_t rEnd = r.end;
        bool prevHadAnchor = false;
        while (pos < rEnd && budget > 0) {
            if (scanNowMs() >= deadline) {
                timeUp = true;
                break;
            }
            const size_t want = static_cast<size_t>(
                std::min<uintptr_t>(chunk, rEnd - pos));
            const ssize_t got =
                ::pread(memFd, buf.data(), want, static_cast<off_t>(pos));
            if (got <= 0) {
                pos += want;  // skip unreadable span, keep going
                prevHadAnchor = false;
                continue;
            }

            // anchor occurrences inside this chunk (ASCII + UTF-16LE bytes)
            const std::string_view sv(buf.data(), static_cast<size_t>(got));
            std::vector<uintptr_t> anchors;
            {
                size_t off = 0;
                while (off < sv.size()) {
                    size_t f8 = sv.find(anchor, off);
                    size_t f16 = sv.find(p16, off);
                    size_t hit = std::string_view::npos;
                    if (f8 != std::string_view::npos &&
                        (f16 == std::string_view::npos || f8 < f16)) {
                        hit = f8;
                    } else if (f16 != std::string_view::npos) {
                        hit = f16;
                    }
                    if (hit == std::string_view::npos) break;
                    anchors.push_back(pos + hit);
                    off = hit + 1;
                }
            }

            // The memmem above is the only work non-anchor chunks get: the
            // word harvest (the expensive part) runs only when this chunk or
            // the previous one held an anchor copy.
            const bool here = !anchors.empty();
            if (!here && !prevHadAnchor) {
                pos += static_cast<uintptr_t>(got);
                prevHadAnchor = false;
                continue;
            }
            budget -= static_cast<size_t>(got);
            // a name can straddle the chunk border: keep harvesting the chunk
            // that immediately follows one containing an anchor
            prevHadAnchor = here;

            // soft proximity: distance to the nearest anchor in this chunk,
            // capped so absence of an anchor is a flat small penalty
            const auto proxDist = [&](uintptr_t absStart) -> int {
                int best = static_cast<int>(kProxCapBytes);
                for (uintptr_t a : anchors) {
                    const long long d =
                        absStart > a ? static_cast<long long>(absStart) -
                                           static_cast<long long>(a)
                                     : static_cast<long long>(a) -
                                           static_cast<long long>(absStart);
                    if (d < static_cast<long long>(best))
                        best = static_cast<int>(d);
                }
                return best;
            };

            const auto note = [&](const std::string& text, uintptr_t absStart) {
                const int dist = proxDist(absStart);
                auto it = index.find(text);
                if (it != index.end()) {
                    ScanHit& h = hits[it->second];
                    ++h.count;
                    if (dist < h.minDist) h.minDist = dist;
                } else if (hits.size() < kMaxCandidates) {
                    index.emplace(text, hits.size());
                    hits.push_back({text, dist, 1});
                }
            };

            // UTF-16LE runs (both byte parities, aligned to absolute address)
            for (int parity = 0; parity < 2; ++parity) {
                size_t i = (static_cast<size_t>(parity) ^ (pos & 1)) & 1;
                while (i + 1 < static_cast<size_t>(got)) {
                    const unsigned char lo = static_cast<unsigned char>(buf[i]);
                    const unsigned char hi =
                        static_cast<unsigned char>(buf[i + 1]);
                    // a readable UTF-16LE unit: ASCII (hi==0) or non-ASCII
                    const bool unitOk =
                        (lo >= 0x20 && lo < 0x7f && hi == 0x00) ||
                        (lo >= 0xa0 && hi != 0x00);
                    if (!unitOk) {
                        i += 2;
                        continue;
                    }
                    const size_t runStart = i;
                    size_t units = 0;
                    while (i + 1 < static_cast<size_t>(got)) {
                        const unsigned char l2 =
                            static_cast<unsigned char>(buf[i]);
                        const unsigned char h2 =
                            static_cast<unsigned char>(buf[i + 1]);
                        const bool ok =
                            (l2 >= 0x20 && l2 < 0x7f && h2 == 0x00) ||
                            (l2 >= 0xa0 && h2 != 0x00);
                        if (!ok) break;
                        ++units;
                        i += 2;
                    }
                    if (units < 3 || units > 64) {
                        i += 2;  // step over the run terminator
                        continue;
                    }
                    std::string text;
                    appendU16ToUtf8(buf, runStart, units, text);
                    note(text, pos + runStart);
                }
            }

            // UTF-8 / plain-ASCII word runs (server names, realm titles and
            // player names are stored as std::string, i.e. UTF-8 in this
            // engine). Scan one-byte printable runs and re-split them on
            // non-name punctuation so a JSON-ish blob like
            //   "name":"CubeCraft","hostname":...
            // still yields the bare "CubeCraft" candidate.
            {
                size_t j = 0;
                size_t wordsNoted = 0;
                while (j < static_cast<size_t>(got)) {
                    const unsigned char c = static_cast<unsigned char>(buf[j]);
                    if (c < 0x20 || c >= 0x7f) {  // only printable ASCII spans
                        ++j;
                        continue;
                    }
                    const size_t runStart = j;
                    size_t runLen = 0;
                    while (j < static_cast<size_t>(got)) {
                        const unsigned char c2 =
                            static_cast<unsigned char>(buf[j]);
                        if (c2 < 0x20 || c2 >= 0x7f) break;
                        ++runLen;
                        ++j;
                    }
                    if (runLen < 3) continue;
                    // split the printable run into word-ish chunks on the
                    // punctuation the name filter would reject anyway
                    const size_t runEnd = runStart + runLen;
                    size_t wordStart = runStart;
                    for (size_t k = runStart; k < runEnd; ++k) {
                        const unsigned char wc =
                            static_cast<unsigned char>(buf[k]);
                        const bool wordChar =
                            (wc >= 'a' && wc <= 'z') ||
                            (wc >= 'A' && wc <= 'Z') ||
                            (wc >= '0' && wc <= '9') || wc == ' ' ||
                            wc == '\'' || wc == '-' || wc == '&' || wc == '!';
                        if (!wordChar) {
                            const size_t wl = k - wordStart;
                            if (wl >= 3 && wl <= 64) {
                                note(std::string(&buf[wordStart], wl),
                                     pos + wordStart);
                                ++wordsNoted;
                            }
                            wordStart = k + 1;
                        }
                    }
                    const size_t wl = runEnd - wordStart;
                    if (wl >= 3 && wl <= 64) {
                        note(std::string(&buf[wordStart], wl),
                             pos + wordStart);
                        ++wordsNoted;
                    }
                    if (wordsNoted >= 768) break;  // sanity cap per chunk
                }
            }
            pos += static_cast<uintptr_t>(got);
        }
        if (timeUp) {
            gCursor = (first + ri) % nRegions;  // resume here next refresh
            break;
        }
    }
    ::close(memFd);
    std::sort(hits.begin(), hits.end(),
              [](const ScanHit& a, const ScanHit& b) {
                  if (a.count != b.count) return a.count > b.count;
                  return a.minDist < b.minDist;
              });
    return hits;
}

// ---------------------------------------------------------------------------
// memmem-only whole-heap counter (dimension markers): same region snapshot and
// per-pass time budget as collectScanHits, but only counts raw byte
// occurrences - no word harvesting, so it is safe on the game thread.
// ---------------------------------------------------------------------------
static void countOccurrencesMany(const std::vector<std::string>& needles,
                                 std::vector<int>& counts) {
    counts.assign(needles.size(), 0);
    if (needles.empty()) return;
    const auto regions = readMemRegions();
    const size_t chunk = 1u << 20;
    std::vector<char> buf(chunk);
    const size_t nRegions = regions.size();
    static size_t gOccCursor = 0;
    if (gOccCursor >= nRegions) gOccCursor = 0;
    const size_t first = gOccCursor;
    const long long deadline = scanNowMs() + kScanTimeBudgetMs;
    int memFd = ::open("/proc/self/mem", O_RDONLY);
    if (memFd < 0) return;
    for (size_t ri = 0; ri < nRegions; ++ri) {
        const MemRegion& r = regions[(first + ri) % nRegions];
        uintptr_t pos = r.start;
        while (pos < r.end) {
            if (scanNowMs() >= deadline) {
                gOccCursor = (first + ri) % nRegions;
                break;
            }
            const size_t want = static_cast<size_t>(
                std::min<uintptr_t>(chunk, r.end - pos));
            const ssize_t got =
                ::pread(memFd, buf.data(), want, static_cast<off_t>(pos));
            if (got <= 0) {
                pos += want;
                continue;
            }
            const std::string_view sv(buf.data(), static_cast<size_t>(got));
            for (size_t k = 0; k < needles.size(); ++k) {
                if (needles[k].empty()) continue;
                size_t off = 0;
                while ((off = sv.find(needles[k], off)) !=
                       std::string_view::npos) {
                    ++counts[k];
                    off += 1;
                    if (counts[k] > 100000) break;
                }
            }
            pos += static_cast<uintptr_t>(got);
        }
        if (scanNowMs() >= deadline) break;
    }
    ::close(memFd);
}

// ---------------------------------------------------------------------------
// name picking: strict filters, then score = occurrences, closeness, style
// ---------------------------------------------------------------------------
static bool isAlphaChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool looksLikeServerName(const std::string& s, const std::string& anchor,
                                int minLen = 4, bool requireTitle = true) {
    const std::string t = trim(s);  // kill trailing spaces BEFORE length checks
    if (t.size() < (size_t)minLen || t.size() > 48) return false;
    int alpha = 0, digit = 0;
    for (char c : t) {
        if (isAlphaChar(c))
            ++alpha;
        else if (c >= '0' && c <= '9')
            ++digit;
        else if (requireTitle) {
            // server names may contain spaces and a few separators
            if (c != ' ' && c != '\'' && c != '-' && c != '&' && c != '!' &&
                c != ':')
                return false;
        } else {
            // IGN (gamertag): letters, digits and underscores only - no
            // spaces/punctuation, so "I'-V" or "ld-linux-x86-64"-class junk
            // can never pass as a handle
            if (c != '_') return false;
        }
    }
    if (alpha < 2 || digit > alpha) return false;  // no all-numeric strings
    // server/realm names are Title Cased; the whole-heap scan otherwise
    // drowns in lowercase loader/path fragments ("libstdc", "usr", "lib")
    if (requireTitle && !(t[0] >= 'A' && t[0] <= 'Z')) return false;
    // never echo the anchor itself or its bare hostname labels ("mco",
    // "cubecraft", "net") - but a Title-Cased brand like "CubeCraft" is a
    // real name that happens to contain the domain word, so keep it
    const std::string lower = lowerAscii(t);
    const std::string anchorLower = lowerAscii(anchor);
    if (lower == anchorLower) return false;
    if (lower.find('.') != std::string::npos) return false;  // dotted host/path
    {
        size_t pos = 0;
        while (pos < anchorLower.size()) {
            size_t dot = anchorLower.find('.', pos);
            const std::string label = anchorLower.substr(
                pos, dot == std::string::npos ? std::string::npos : dot - pos);
            if (label.size() >= 3 && lower == label &&
                !(t[0] >= 'A' && t[0] <= 'Z'))
                return false;  // bare lowercase label = hostname fragment
            if (dot == std::string::npos) break;
            pos = dot + 1;
        }
    }
    static const char* kBad[] = {
        "minecraft", "server", "realm", "realms", "play", "join", "invite",
        "friends", "friend", "settings", "menu", "options", "exit", "back",
        "cancel", "accept", "world", "worlds", "marketplace", "news",
        "achievements", "wardrobe", "skins", "store", "profile", "online",
        "offline", "loading", "connecting", "featured", "servers", "custom",
        "add", "search", "refresh", "public", "private", "more", "pause",
        "game", "language", "accessibility", "control", "video", "audio",
        "account", "sign in", "sign out", "host", "port", "address", "name",
        "players", "player", "xbox", "live", "hostname", "uri", "resturi",
        "resource", "description", "splash", "subtitle", "ip", "size",
        "content", "category", "genre", "author", "color", "image", "icon",
        "texture", "the nether", "overworld", "theend", "the end",
        "mojang", "multiplayer", "minecraftpe", "realmsplus", "experimental",
        // short high-frequency words (matter mostly in IGN mode, minLen 3)
        "the", "and", "you", "for", "are", "but", "not", "was", "his", "her",
        "its", "our", "all", "any", "can", "did", "get", "had", "has", "him",
        "how", "now", "old", "see", "she", "two", "use", "who", "why", "may",
        "let", "one", "day", "new", "fun", "god", "your", "from", "have",
        "with", "this", "they", "will", "that", "what", "when", "where",
        "which", "there", "here", "then", "them", "some", "out", "into",
        "over", "under", "about", "after", "before", "again", "also",
        "down", "up", "off", "don't", "can't", "won't",
        // game UI / mode / scoreboard noise
        "team", "chat", "lobby", "lobbies", "hub", "lava", "sky", "egg",
        "wars", "walls", "bed", "beds", "build", "builds", "pvp", "ffa",
        "solo", "duo", "squad", "kit", "kits", "rank", "ranks", "coins",
        "emerald", "emeralds", "score", "scoreboard", "objective",
        "sidebar", "creative", "survival", "hardcore", "peaceful", "easy",
        "normal", "hunger", "health", "spawn", "respawn", "death",
        "teleport", "position", "coordinates", "distance", "minutes",
        "seconds", "weather", "raining", "thunder", "time", "ticks",
        "error", "exception", "warning", "invalid", "unknown",
        "default", "empty", "null", "undefined", "loading", "connecting to",
        "windows", "platform", "xuid", "linux", "android", "ios", "macos",
        "java", "bedrock", "console", "xbox one", "playstation", "switch",
        // known junk fragments from misaligned/mixed reads
        "ems", "nms", "ss5", "dss", "dru", "drU", "subc", "mnllno",
        "onn", "one", "ome", "hoi", "ems",
        // dynamic-loader and file-cache path fragments (whole-heap noise)
        "libc", "libm", "libz", "libdl", "libpthread", "libstdc", "libgcc",
        "vsyscall", "vvar", "vdso", "ld-linux", "libmvec", "usr", "lock",
        "tmp", "etc", "proc", "dev", "sys", "run", "var", "home", "root",
        "bin", "sbin", "data", "cache", "json", "png", "ogg", "wav", "jpg",
        "mcpack", "packs", "userdata"};
    const std::string lowerTrim = lowerAscii(t);
    for (const char* b : kBad)
        if (lowerTrim == b) return false;
    return true;
}

static std::string pickBestName(const std::string& anchor,
                                const std::vector<ScanHit>& hits,
                                std::string* debugOut, int minLen = 4) {
    const bool requireTitle = minLen >= 4;  // IGN mode relaxes title-casing
    std::string best;
    long long bestScore = 0;
    std::vector<std::pair<long long, std::string>> cands;  // (score, text)
    for (const auto& h : hits) {
        // a real name is copied by the game into several heap strings; a
        // single-copy hit is far more likely a decode artifact ("O9mU"-class)
        // single copy is still decisive when it sits within ~4 KiB of the anchor
        // (the same session/profile JSON blob); otherwise require 2+
        if (h.count < (h.minDist <= 4096 ? 1 : 2)) continue;
        if (!looksLikeServerName(h.text, anchor, minLen, requireTitle))
            continue;
        const std::string t = trim(h.text);
        const bool hasUpper = std::any_of(
            t.begin(), t.end(),
            [](char c) { return c >= 'A' && c <= 'Z'; });
        // IGN mode allows lowercase names, but far-flung lowercase loader
        // and file-cache junk must not win: lowercase candidates either need
        // an uppercase (mixed-case IGN) or must sit near an anchor AND be
        // long enough to be a real handle rather than a short junk word.
        if (!requireTitle && !hasUpper &&
            (h.minDist > 256 * 1024 || t.size() < 5))
            continue;
        long long score = static_cast<long long>(h.count) * 30 -
                          static_cast<long long>(h.minDist) / 1024;
        if (hasUpper) score += 4;  // Title Case
        if (t.size() >= 4 && t.size() <= 24) score += 2;
        cands.emplace_back(score, t);
        if (score > bestScore) {
            bestScore = score;
            best = t;
        }
    }
    std::sort(cands.begin(), cands.end(),
              [](const std::pair<long long, std::string>& a,
                 const std::pair<long long, std::string>& b) {
                  if (a.first != b.first) return a.first > b.first;
                  return a.second.size() > b.second.size();  // prefer longer
              });
    if (debugOut) {
        debugOut->clear();
        for (size_t i = 0; i < cands.size() && i < 8; ++i) {
            if (i) *debugOut += ", ";
            *debugOut += std::to_string(cands[i].first) + ":" + cands[i].second;
        }
    }
    return best;
}