#pragma once

#include <cstddef>
#include <string_view>

namespace ra_commands::signatures
{
    enum class AddressKind
    {
        Match,
        Absolute32Operand
    };

    struct SignatureSpec
    {
        std::string_view Name;
        std::string_view Section;
        std::string_view Pattern;
        AddressKind Kind = AddressKind::Match;
        std::size_t InstructionSize = 0;
        std::size_t OperandOffset = 0;
    };
}
