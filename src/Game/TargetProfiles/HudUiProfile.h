#pragma once
#include <array>
#include <cstdint>

namespace ra_commands::game::hud_profile
{
    // gamemd.exe 7cd…cd0747；DrawOnTop 内鼠标 undraw 后、Tactical 三阶段之前。
    inline constexpr std::uintptr_t RESTORE_SITE = 0x004F44A5u;
    inline constexpr std::array<std::uint8_t, 10> RESTORE_BYTES =
        {0x8B, 0x46, 0x0C, 0xC7, 0x46, 0x0C, 0x00, 0x00, 0x00, 0x00};
    // 原始 CALL 带 EDI 隐式输入；桥接不得用普通 thiscall 包装丢弃它。
    inline constexpr std::uintptr_t DRAW_SITE = 0x004F455Du;
    inline constexpr std::uintptr_t MESSAGE_DRAW = 0x005D49A0u;
    inline constexpr std::array<std::uint8_t, 5> DRAW_BYTES = {0xE8, 0x3E, 0x04, 0x0E, 0x00};
    // 4A59E0: ECX=outRect, EDX=text；Point2D 值和 flags/margins 共五个栈槽，RET 14h。
    inline constexpr std::uintptr_t GET_TEXT_DIMENSIONS_ADDRESS = 0x004A59E0u;
    inline constexpr std::array<std::uint8_t, 16> GET_TEXT_DIMENSIONS_ENTRY = {
        0x53, 0x55, 0x56, 0x57, 0x8B, 0x3D, 0xD0, 0xC4,
        0x89, 0x00, 0x8B, 0xF1, 0x6A, 0x00, 0x52, 0x8B};
    inline constexpr std::uintptr_t GET_TEXT_DIMENSIONS_RETURN_ADDRESS = 0x004A5A47u;
    inline constexpr std::array<std::uint8_t, 3> GET_TEXT_DIMENSIONS_RETURN_ENTRY = {0xC2, 0x14, 0x00};
    // 4A5EB0: ECX=outPoint, EDX=text；Surface/bounds/location/color/三项样式共七个栈槽。
    inline constexpr std::uintptr_t DRAW_TEXT_ADDRESS = 0x004A5EB0u;
    inline constexpr std::array<std::uint8_t, 16> DRAW_TEXT_ENTRY = {
        0x83, 0xEC, 0x28, 0x8B, 0x44, 0x24, 0x34, 0x53,
        0x55, 0x56, 0x8B, 0x68, 0x04, 0x57, 0x8B, 0x7C};
    inline constexpr std::uintptr_t DRAW_TEXT_RETURN_ADDRESS = 0x004A6002u;
    inline constexpr std::array<std::uint8_t, 3> DRAW_TEXT_RETURN_ENTRY = {0xC2, 0x1C, 0x00};
}
