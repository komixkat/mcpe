#include "discord_ipc.h"

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {
constexpr std::uint32_t kFrameHeaderSize = 8;
constexpr int kHandshakeTimeoutMs = 5000;
constexpr int kPayloadReadTimeoutMs = 30000;

// Best effort extraction of a JSON "message" string (used for CLOSE frames and
// SET_ACTIVITY error acks). Returns "" when not found.
std::string extractMessage(const std::string& payload) {
    const char* key = "\"message\":\"";
    size_t i = payload.find(key);
    if (i == std::string::npos) return "";
    i += std::strlen(key);
    std::string out;
    bool esc = false;
    for (size_t j = i; j < payload.size(); ++j) {
        char c = payload[j];
        if (esc) {
            out += c;
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '"') break;
        out += c;
    }
    return out;
}

// True when the payload contains "field":"value" (or "field" : "value" with
// optional whitespace). Tolerates any whitespace so the client works with
// both compact (Discord's own) and pretty-printed JSON.
bool jsonHasField(const std::string& payload, const std::string& field,
                  const std::string& value) {
    size_t i = 0;
    while ((i = payload.find(field, i)) != std::string::npos) {
        size_t j = i + field.size();
        while (j < payload.size() && (payload[j] == ' ' || payload[j] == '\t')) ++j;
        if (j < payload.size() && payload[j] == ':') {
            ++j;
            while (j < payload.size() && (payload[j] == ' ' || payload[j] == '\t')) ++j;
            if (payload.compare(j, value.size(), value) == 0) return true;
        }
        i += field.size();
    }
    return false;
}
}  // namespace

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

DiscordIpc::DiscordIpc() = default;
DiscordIpc::~DiscordIpc() { disconnect(); }

void DiscordIpc::disconnect() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool DiscordIpc::openSocket(const std::string& path) {
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) return false;
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return false;
    }
    fd_ = fd;
    return true;
}

bool DiscordIpc::sendFrame(Opcode op, const std::string& payload) {
    if (fd_ < 0) return false;
    std::uint32_t header[2] = {static_cast<std::uint32_t>(op),
                               static_cast<std::uint32_t>(payload.size())};

    const char* buf = reinterpret_cast<const char*>(header);
    size_t left = sizeof(header);
    while (left > 0) {
        ssize_t n = ::send(fd_, buf, left, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            disconnect();
            return false;
        }
        buf += n;
        left -= static_cast<size_t>(n);
    }

    left = payload.size();
    buf = payload.data();
    while (left > 0) {
        ssize_t n = ::send(fd_, buf, left, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            disconnect();
            return false;
        }
        buf += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

DiscordIpc::ReadResult DiscordIpc::readFrame(Opcode& op, std::string& payload,
                                             int timeoutMs) {
    payload.clear();
    if (fd_ < 0) return ReadResult::Error;

    std::uint8_t header[kFrameHeaderSize];
    size_t got = 0;
    while (got < sizeof(header)) {
        struct pollfd pfd;
        pfd.fd = fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int r = ::poll(&pfd, 1, timeoutMs);
        if (r < 0) {
            if (errno == EINTR) continue;
            disconnect();
            return ReadResult::Error;
        }
        if (r == 0) {
            // A timeout while the header is half-read would desync the stream.
            if (got > 0) {
                disconnect();
                return ReadResult::Error;
            }
            return ReadResult::Timeout;
        }
        ssize_t n = ::recv(fd_, header + got, sizeof(header) - got, 0);
        if (n <= 0) {
            disconnect();
            return ReadResult::Error;
        }
        got += static_cast<size_t>(n);
    }

    std::uint32_t opcode = 0;
    std::uint32_t len = 0;
    std::memcpy(&opcode, header, 4);
    std::memcpy(&len, header + 4, 4);
    op = static_cast<Opcode>(opcode);

    if (len > kFrameHeaderSize * 1024) {  // sanity cap (~8 KiB)
        disconnect();
        return ReadResult::Error;
    }
    if (len > 0) {
        payload.resize(len);
        got = 0;
        while (got < len) {
            struct pollfd pfd;
            pfd.fd = fd_;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int r = ::poll(&pfd, 1, kPayloadReadTimeoutMs);
            if (r <= 0) {
                disconnect();
                return ReadResult::Error;
            }
            ssize_t n = ::recv(fd_, &payload[0] + got, len - got, 0);
            if (n <= 0) {
                disconnect();
                return ReadResult::Error;
            }
            got += static_cast<size_t>(n);
        }
    }
    return ReadResult::Got;
}

void DiscordIpc::consumeFrame(Opcode op, const std::string& payload) {
    if (op == Opcode::Ping) {
        sendFrame(Opcode::Pong, payload);  // Discord expects the same payload back
        return;
    }
    if (op == Opcode::Close) {
        lastError_ = extractMessage(payload);
        if (lastError_.empty()) lastError_ = "Discord closed the connection";
        disconnect();
        return;
    }
    if (op == Opcode::Frame) {
        // SET_ACTIVITY acks carry the nonce we sent; surface errors only.
        if (!lastNonce_.empty() && payload.find(lastNonce_) != std::string::npos &&
            jsonHasField(payload, "\"evt\"", "\"ERROR\"")) {
            lastError_ = extractMessage(payload);
            if (lastError_.empty()) lastError_ = "Discord rejected SET_ACTIVITY";
        }
        return;
    }
}

bool DiscordIpc::connect(const std::string& clientId) {
    disconnect();
    lastError_.clear();

    std::vector<std::string> candidates;
    char buf[160];
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    for (int i = 0; i < 10; ++i) {
        std::snprintf(buf, sizeof(buf), "/tmp/discord-ipc-%d", i);
        candidates.emplace_back(buf);
        if (runtime && *runtime) {
            std::snprintf(buf, sizeof(buf), "%s/discord-ipc-%d", runtime, i);
            candidates.emplace_back(buf);
        }
    }

    for (const auto& path : candidates) {
        if (!openSocket(path)) continue;

        std::string hs = "{\"v\":1,\"client_id\":\"" + jsonEscape(clientId) + "\"}";
        if (!sendFrame(Opcode::Handshake, hs)) {
            disconnect();
            return false;
        }

        bool ready = false;
        bool refused = false;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kHandshakeTimeoutMs);
        while (!ready && !refused) {
            auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now())
                              .count();
            if (remain <= 0) break;
            Opcode op;
            std::string payload;
            ReadResult res = readFrame(op, payload, static_cast<int>(remain));
            if (res == ReadResult::Got) {
                if (op == Opcode::Frame) {
                    if (jsonHasField(payload, "\"evt\"", "\"READY\"")) {
                        ready = true;
                    } else if (jsonHasField(payload, "\"evt\"", "\"ERROR\"")) {
                        lastError_ = extractMessage(payload);
                        if (lastError_.empty()) lastError_ = "Discord rejected the handshake";
                        refused = true;
                    }
                } else if (op == Opcode::Close) {
                    lastError_ = extractMessage(payload);
                    if (lastError_.empty()) lastError_ = "Discord closed the connection";
                    refused = true;
                } else if (op == Opcode::Ping) {
                    sendFrame(Opcode::Pong, payload);
                }
            } else {
                lastError_ = "connection lost during handshake";
                refused = true;
            }
        }

        if (ready) return true;
        disconnect();
        // A socket that connected but refused the handshake is definitive: do
        // not keep probing the remaining candidates.
        if (refused && lastError_ != "connection lost during handshake") return false;
    }

    if (lastError_.empty()) lastError_ = "no Discord client found";
    return false;
}

bool DiscordIpc::setActivity(const std::string& state, const std::string& details,
                             std::int64_t startMs) {
    if (fd_ < 0) return false;
    ++nonceCounter_;
    char nonce[48];
    std::snprintf(nonce, sizeof(nonce), "mcpe-%u", nonceCounter_);
    lastNonce_ = nonce;

    std::string activity;
    activity += "{\"state\":\"" + jsonEscape(state) + "\",";
    activity += "\"details\":\"" + jsonEscape(details) + "\",";
    activity += "\"timestamps\":{\"start\":" + std::to_string(startMs) + "},";
    activity += "\"instance\":true,\"type\":0}";

    std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
                          std::to_string(static_cast<long>(getpid())) +
                          ",\"activity\":" + activity + "},\"nonce\":\"" + nonce + "\"}";
    lastError_.clear();
    return sendFrame(Opcode::Frame, payload);
}

bool DiscordIpc::pump(int timeoutMs) {
    while (fd_ >= 0) {
        Opcode op;
        std::string payload;
        ReadResult res = readFrame(op, payload, timeoutMs);
        if (res == ReadResult::Timeout) return true;
        if (res != ReadResult::Got) return false;
        consumeFrame(op, payload);
    }
    return false;
}