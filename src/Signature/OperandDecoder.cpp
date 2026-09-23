#include "Signature/OperandDecoder.h"

#include <cstring>
#include <limits>

namespace ra_commands::memory
{
    namespace
    {
        template<typename T>
        bool TryReadOperand(
            std::span<const std::byte> instruction,
            std::size_t operandOffset,
            T& outValue)
        {
            if (operandOffset > instruction.size() ||
                sizeof(T) > instruction.size() - operandOffset)
            {
                return false;
            }

            std::memcpy(&outValue, instruction.data() + operandOffset, sizeof(T));
            return true;
        }
    }

    bool TryDecodeAbsolute32(
        std::span<const std::byte> instruction,
        std::size_t operandOffset,
        std::uintptr_t& outAddress)
    {
        std::uint32_t value = 0;
        if (!TryReadOperand(instruction, operandOffset, value))
        {
            return false;
        }
        outAddress = value;
        return true;
    }

    bool TryDecodeRelative32(
        std::uintptr_t instructionAddress,
        std::span<const std::byte> instruction,
        std::size_t operandOffset,
        std::size_t instructionSize,
        std::uintptr_t& outAddress)
    {
        std::int32_t displacement = 0;
        if (instructionSize > instruction.size() ||
            operandOffset > instructionSize ||
            sizeof(displacement) > instructionSize - operandOffset ||
            !TryReadOperand(instruction, operandOffset, displacement))
        {
            return false;
        }

        const auto target = static_cast<std::int64_t>(instructionAddress) +
            static_cast<std::int64_t>(instructionSize) + displacement;
        if (target <= 0 ||
            target > static_cast<std::int64_t>((std::numeric_limits<std::uintptr_t>::max)()))
        {
            return false;
        }
        outAddress = static_cast<std::uintptr_t>(target);
        return true;
    }

    bool TryDecodeFieldOffset(
        std::span<const std::byte> instruction,
        std::size_t operandOffset,
        std::size_t widthBytes,
        std::size_t& outFieldOffset)
    {
        if (widthBytes == 1)
        {
            std::uint8_t value = 0;
            if (!TryReadOperand(instruction, operandOffset, value))
            {
                return false;
            }
            outFieldOffset = value;
            return true;
        }

        if (widthBytes == 4)
        {
            std::uint32_t value = 0;
            if (!TryReadOperand(instruction, operandOffset, value))
            {
                return false;
            }
            outFieldOffset = value;
            return true;
        }

        return false;
    }
}
