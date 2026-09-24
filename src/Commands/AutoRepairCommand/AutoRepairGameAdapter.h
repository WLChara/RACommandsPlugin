#pragma once

#include "Commands/AutoRepairCommand/IAutoRepairGamePort.h"

namespace ra_commands::game
{
    class AutoRepairGameAdapter final : public auto_repair::IAutoRepairGamePort
    {
    public:
        [[nodiscard]] bool IsMatchReady() const override;
        [[nodiscard]] std::uintptr_t GetSessionIdentity() const override;
        [[nodiscard]] std::uint32_t GetCurrentFrame() const override;
        [[nodiscard]] bool CaptureSnapshot(auto_repair::Snapshot& outSnapshot) const override;
        [[nodiscard]] std::uint32_t GetNativeFreeSlots() const override;
        [[nodiscard]] bool TryRepair(auto_repair::BuildingId id) const override;
    };
}
