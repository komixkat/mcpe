#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

//hashedString model used by MCUIRC::flushImages
class HashedString {
public:
    std::uint64_t mStrHash;
    std::string mStr;
    mutable const HashedString* mLastMatch;

public:
    HashedString() : mStrHash(0), mStr(), mLastMatch(nullptr) {}

    explicit HashedString(std::string_view str)
        : mStrHash(computeHash(str)), mStr(str), mLastMatch(nullptr) {}

    explicit HashedString(const char* str)
        : HashedString(str ? std::string_view(str) : std::string_view()) {}

    explicit HashedString(std::string str)
        : mStrHash(computeHash(str)), mStr(std::move(str)), mLastMatch(nullptr) {}

private:
    static std::uint64_t computeHash(std::string_view str) {
        if (str.empty())
            return 0;

        constexpr std::uint64_t Offset = 0xCBF29CE484222325ULL;
        constexpr std::uint64_t Prime = 0x100000001B3ULL;

        std::uint64_t hash = Offset;
        for (char ch : str)
            hash = static_cast<std::uint64_t>(static_cast<unsigned char>(ch)) ^ (Prime * hash);

        return hash;
    }
};
