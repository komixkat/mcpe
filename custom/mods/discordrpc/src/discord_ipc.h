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

// Everything that can go into a SET_ACTIVITY payload. Empty strings / zero
// sizes are omitted from the JSON so only configured fields are sent.
struct Activity {
    std::string state;      // rich presence line 3 (the "big" text area, below details)
    std::string details;    // rich presence line 2
    std::int64_t startMs = 0;
    // assets (image keys registered in the Discord application)
    std::string largeImage, largeText, smallImage, smallText;
    // party + join ("Join Game" button)
    std::string partyId;
    int partySize = 0;
    int partyMax = 0;
    std::string joinSecret;
};

// A friend clicked "Join Game" on our profile. Filled by pump().
struct JoinRequest {
    std::string userId;
    std::string username;
    std::string secret;
    bool valid = false;
};

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

    // Send one SET_ACTIVITY frame with the given activity. Returns false when
    // the connection died.
    bool setActivity(const Activity& a);

    // Read and handle frames for up to timeoutMs, answering pings, collecting
    // ack errors and filling lastJoin() on ACTIVITY_JOIN dispatches. Returns
    // false when the connection died (the socket is closed in that case and
    // the caller should reconnect).
    bool pump(int timeoutMs);

    // Most recent Join Game request seen by pump(), or an invalid one.
    const JoinRequest& lastJoin() const { return lastJoin_; }
    void clearJoin() { lastJoin_ = JoinRequest{}; }

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
    JoinRequest lastJoin_;
};