#pragma once

#include "Commands/TeslaChargeCommand/ITeslaChargeGamePort.h"

namespace ra_commands::game
{
    class TeslaChargeGameAdapter final : public tesla_charge::ITeslaChargeGamePort
    {
    public:
        [[nodiscard]] std::uint32_t GetCurrentFrame() const override;
        [[nodiscard]] bool CaptureSnapshot(tesla_charge::Snapshot& outSnapshot) const override;
        [[nodiscard]] bool MakeAttackIntent(
            tesla_charge::UnitId charger, tesla_charge::UnitId tesla,
            std::uint32_t epoch, commands::ClickedMissionIntent& outIntent) const override;
        [[nodiscard]] bool IsTargetingTesla(
            tesla_charge::UnitId charger, tesla_charge::UnitId tesla) const override;
        void DeselectIfSelected(tesla_charge::UnitId charger) const override;
    };
}
