#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ra_commands::memory
{
    // 只解释调用方提供的指令字节；不读取进程内存或持有目标游戏的 Sig。
    bool TryDecodeAbsolute32(
        std::span<const std::byte> instruction,
        std::size_t operandOffset,
        std::uintptr_t& outAddress);

    bool TryDecodeRelative32(
        std::uintptr_t instructionAddress,
        std::span<const std::byte> instruction,
        std::size_t operandOffset,
        std::size_t instructionSize,
        std::uintptr_t& outAddress);

    bool TryDecodeFieldOffset(
        std::span<const std::byte> instruction,
        std::size_t operandOffset,
        std::size_t widthBytes,
        std::size_t& outFieldOffset);
}
