#pragma once

#include "Commands/TeslaChargeCommand/ITeslaChargeGamePort.h"
#include "ClickedMission/ClickedMissionDispatcher.h"

#include <cstdint>
#include <vector>

namespace ra_commands::tesla_charge
{
    class TeslaChargeCommandService final
    {
    public:
        TeslaChargeCommandService(
            ITeslaChargeGamePort& game, commands::ClickedMissionDispatcher& dispatcher);

        void OnHotkey();
        void OnGameFrame();
        void Reset();
        [[nodiscard]] bool IsEnabled() const noexcept;

    private:
        void MaintainAssignments();
        void DeselectAssigned();

        ITeslaChargeGamePort& mGame;
        commands::ClickedMissionDispatcher& mDispatcher;
        std::vector<Assignment> mAssignments;
        std::uint32_t mEpoch = 0;
        std::uint32_t mLastMaintenanceFrame = 0;
        std::uint32_t mLastDeselectFrame = 0;
        bool mEnabled = false;
    };
}
