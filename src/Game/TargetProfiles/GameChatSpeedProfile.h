#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ra_commands::game::chat_speed_profile
{
    // gamemd.exe SHA-256 7cd005d2...。只校验实际使用的同步事件及战役门控；
    // 不调用原生速度菜单，因此不能依赖已由 RAReplayPlugin 挂钩的 4E1E1B。
    inline constexpr std::uintptr_t SESSION_MODE_ADDRESS = 0xA8B238;
    inline constexpr std::uintptr_t NETWORK_ACTIVE_ADDRESS = 0xA8E9A0;
    inline constexpr std::uintptr_t CAMPAIGN_SPEED_UNLOCKED_ADDRESS = 0xA8EDDC;
    inline constexpr std::uintptr_t GAME_SPEED_ADDRESS = 0xA8EB60;

    struct Probe
    {
        std::uintptr_t mAddress;
        std::array<std::uint8_t, 18> mBytes;
        std::size_t mSize;
    };
    inline constexpr std::array<Probe, 2> PROBES = {{
        {0x4C794B, {0x8B,0x56,0x07,0x89,0x15,0x60,0xEB,0xA8,0}, 9},
        {0x55D767, {0xA0,0xDC,0xED,0xA8,0,0x84,0xC0,0x75,0x2E,0x8D,0x4C,0x24,0x14}, 13}
    }};

    template<typename TReader>
    bool ValidateProbe(TReader&& reader, std::size_t index)
    {
        const auto& probe = PROBES[index];
        std::array<std::uint8_t, 18> bytes{};
        return reader(probe.mAddress, bytes.data(), probe.mSize) &&
            std::equal(probe.mBytes.begin(), probe.mBytes.begin() + probe.mSize, bytes.begin());
    }

    template<typename TReader>
    bool ValidateNetwork(TReader&& reader) { return ValidateProbe(reader, 0); }

    template<typename TReader>
    bool ValidateCampaign(TReader&& reader) { return ValidateProbe(reader, 1); }

    template<typename TReader>
    bool Validate(TReader&& reader)
    {
        return ValidateNetwork(reader) && ValidateCampaign(reader);
    }
}
