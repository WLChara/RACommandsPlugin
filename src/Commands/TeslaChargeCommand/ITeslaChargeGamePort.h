#pragma once

#include "ClickedMission/ClickedMissionQueue.h"
#include "Commands/TeslaChargeCommand/TeslaChargePlanner.h"

#include <cstdint>

namespace ra_commands::tesla_charge
{
    // 游戏对象只在适配器内解析；服务和规划器只保存稳定身份。
    class ITeslaChargeGamePort
    {
    public:
        virtual ~ITeslaChargeGamePort() = default;

        [[nodiscard]] virtual std::uint32_t GetCurrentFrame() const = 0;
        [[nodiscard]] virtual bool CaptureSnapshot(Snapshot& outSnapshot) const = 0;
        [[nodiscard]] virtual bool MakeAttackIntent(
            UnitId charger, UnitId tesla, std::uint32_t epoch,
            commands::ClickedMissionIntent& outIntent) const = 0;
        [[nodiscard]] virtual bool IsTargetingTesla(UnitId charger, UnitId tesla) const = 0;
        virtual void DeselectIfSelected(UnitId charger) const = 0;
    };
}
