#pragma once
#include <array>
#include <cstdint>

namespace ra_commands::game::planning_participant_capacity::profile
{
    // SHA-256 7cd005d263fde203d9c84548200a057a8df61d724da3c6bd1e521eeb61cd0747，PE32基址0x400000。
    inline constexpr std::uintptr_t CAPACITY_COMPARE_ADDRESS = 0x006391A7u;
    inline constexpr std::uintptr_t RESTORE_REGISTERS_ADDRESS = 0x006391ADu;
    inline constexpr std::uintptr_t ACCEPT_CONTINUE = 0x00639203u;
    inline constexpr std::array<std::uint8_t, 6> CAPACITY_COMPARE_BYTES = {
        0x81, 0xFD, 0x80, 0x00, 0x00, 0x00
    };
    inline constexpr std::array<std::uint8_t, 2> RESTORE_REGISTERS_BYTES = {0x5D, 0x5B};
    inline constexpr std::array<std::uint8_t, 4> ACCEPT_BYTES = {0xB0, 0x01, 0x59, 0xC3};
    // 原生参与者计数表包含24个House槽；无效玩家身份不进入数量扩容。
    inline constexpr int HOUSE_COUNTER_SLOTS = 24;
    // cmp 前已 pop edi/pop esi；cmp 后依次 pop ebp/pop ebx，成功段 mov al,1/pop ecx/ret。
    inline constexpr std::uintptr_t REPLAY_FLAGS_ADDRESS = 0x00A8D5F8u;
    inline constexpr std::uint32_t REPLAY_FLAG = 0x02u;
}
