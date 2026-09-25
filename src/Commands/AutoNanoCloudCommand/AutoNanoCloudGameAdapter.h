#pragma once

#include "Commands/AutoNanoCloudCommand/IAutoNanoCloudGamePort.h"

namespace ra_commands::game
{
    class AutoNanoCloudGameAdapter final : public auto_nano_cloud::IAutoNanoCloudGamePort
    {
    public:
        [[nodiscard]] bool CaptureSnapshot(std::optional<auto_nano_cloud::UnitId> retainedVictim,
            auto_nano_cloud::Snapshot& outSnapshot) const override;
        [[nodiscard]] bool MakeStopIntent(auto_nano_cloud::UnitId victim,
            std::uint32_t epoch, commands::ClickedMissionIntent& outIntent) const override;
        [[nodiscard]] bool MakeAttackIntent(auto_nano_cloud::UnitId hunter,
            auto_nano_cloud::UnitId victim, std::uint32_t epoch,
            commands::ClickedMissionIntent& outIntent) const override;
        [[nodiscard]] bool DeselectAndUngroup(auto_nano_cloud::UnitId victim) const override;
    };

    [[nodiscard]] bool ValidateAutoNanoCloudIntent(const commands::ClickedMissionIntent& intent);
    void AttemptAutoNanoCloudIntent(const commands::ClickedMissionIntent& intent);
}
