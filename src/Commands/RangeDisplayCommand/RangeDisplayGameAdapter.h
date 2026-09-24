#pragma once

#include "Commands/RangeDisplayCommand/IRangeDisplayGamePort.h"

namespace ra_commands::game
{
    class RangeDisplayGameAdapter final : public range_display::IRangeDisplayGamePort
    {
    public:
        [[nodiscard]] bool IsMatchReady() const override;
        [[nodiscard]] std::uintptr_t GetSessionIdentity() const override;
        [[nodiscard]] std::uint32_t GetCurrentFrame() const override;
    };
}
