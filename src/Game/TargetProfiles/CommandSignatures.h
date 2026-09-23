#pragma once

#include "Signature/SignatureSpec.h"

namespace ra_commands::signatures
{
    // 当前样本的候选签名；来源及适用范围见 docs/ida-evidence.md。
    inline constexpr SignatureSpec COMMAND_INITIALIZATION = {
        "command initialization", ".text", "A0 ? ? ? ? 83 EC ? 53 33 DB 3A C3"
    };
    inline constexpr SignatureSpec KEYBOARD_HOTKEY_LOADER = {
        "keyboard hotkey loader", ".text", "81 EC ? ? ? ? 53 8D 4C 24"
    };
    inline constexpr SignatureSpec COMMAND_ARRAY_ITEMS = {
        "command array items", ".text", "8B 15 ? ? ? ? 8B F5",
        AddressKind::Absolute32Operand, 6, 2
    };
    inline constexpr SignatureSpec HOTKEY_TABLE = {
        "hotkey table", ".text", "8B 0D ? ? ? ? 56 51 E8",
        AddressKind::Absolute32Operand, 6, 2
    };
}
