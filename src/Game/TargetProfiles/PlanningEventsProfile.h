#pragma once

#include <array>
#include <cstdint>

namespace ra_commands::game::command_capacity::planning_profile
{
    // SHA-256 7cd005d2... 的共享OutList生产入口；不是规划长度或执行帧补丁。
    inline constexpr std::uintptr_t PLANNING_EVENT_ADDRESS = 0x006521C0u;
    inline constexpr std::array<std::uint8_t, 20> ENTRY_SIGNATURE = {
        0xA1, 0x84, 0xED, 0xA8, 0x00, 0x89, 0x44, 0x24, 0x07, 0xA1,
        0xC8, 0x02, 0xA8, 0x00, 0x3D, 0x80, 0x00, 0x00, 0x00, 0x7D
    };
    inline constexpr std::uintptr_t WRITE_INDEX_SIGNATURE_ADDRESS = 0x00652211u;
    inline constexpr std::array<std::uint8_t, 9> WRITE_INDEX_SIGNATURE = {
        0x40, 0x83, 0xE0, 0x7F, 0xA3, 0xD0, 0x02, 0xA8, 0x00
    };
    // 两个分支均RET70h：111字节事件按值传入，Win32参数栈向上对齐至112字节。
    inline constexpr std::uintptr_t RETURN_SIGNATURE_ADDRESS = 0x00652225u;
    inline constexpr std::array<std::uint8_t, 10> RETURN_SIGNATURE = {
        0xB0, 0x01, 0xC2, 0x70, 0x00, 0x32, 0xC0, 0xC2, 0x70, 0x00
    };
}
