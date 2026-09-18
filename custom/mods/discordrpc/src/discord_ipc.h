#pragma once

#include <cstdint>
#include <string>

// Minimal Discord Rich Presence IPC client.
//
// Implements the wire protocol used by the official discord-rpc library on
// Linux: a unix stream socket plus length-prefixed JSON frames.
//   frame = [uint32 LE opcode][uint32 LE payload length][payload]
//   opcodes: 0 = handshake, 1 = frame, 2 = close, 3 = ping, 4 = pong
//
// Everything is synchronous and fail-closed: every socket error is reported as
// a failure and the caller reconnects with backoff (Discord itself is not
// always running when the game is).
class DiscordIpc {
public:
    DiscordIpc();
    ~DiscordIpc();

    DiscordIpc(const DiscordIpc&) = delete;
    DiscordIpc& operator=(const DiscordIpc&) = delete;

    // Probe every /tmp/discord-ipc-N (and the $XDG_RUNTIME_DIR variant),
    // connect, handshake and wait for the READY event. Returns true when the
    // connection is live; lastError() then describes any failure.
    bool connect(const std::string& clientId);

    // Close the socket and forget the connection.
    void disconnect();

    bool connected() const { return fd_ >= 0; }

    // Send one SET_ACTIVITY frame. Returns false when the connection died.
    bool setActivity(const std::string& state, const std::string& details,
                     std::int64_t startMs);

    // Read and handle frames for up to timeoutMs, answering pings and
    // collecting ack errors. Returns false when the connection died (the
    // socket is closed in that case and the caller should reconnect).
    bool pump(int timeoutMs);

    // Last error surfaced by Discord (e.g. unknown client id) or empty.
    const std::string& lastError() const { return lastError_; }

private:
    enum class Opcode : std::uint32_t {
        Handshake = 0,
        Frame = 1,
        Close = 2,
        Ping = 3,
        Pong = 4,
    };

    enum class ReadResult { Got, Timeout, Error };

    bool openSocket(const std::string& path);
    bool sendFrame(Opcode op, const std::string& payload);
    ReadResult readFrame(Opcode& op, std::string& payload, int timeoutMs);
    void consumeFrame(Opcode op, const std::string& payload);

    int fd_ = -1;
    std::string lastError_;
    unsigned nonceCounter_ = 0;
    std::string lastNonce_;
};