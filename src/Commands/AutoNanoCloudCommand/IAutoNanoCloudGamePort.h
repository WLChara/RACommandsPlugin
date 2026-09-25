#pragma once

#include "Commands/AutoNanoCloudCommand/AutoNanoCloudPlanner.h"
#include "ClickedMission/ClickedMissionQueue.h"

#include <cstdint>
#include <optional>

namespace ra_commands::auto_nano_cloud
{
    /** 游戏侧提供值快照与经身份复验的副作用；所有方法只在游戏主线程调用。 */
    class IAutoNanoCloudGamePort
    {
    public:
        virtual ~IAutoNanoCloudGamePort() = default;

        [[nodiscard]] virtual bool CaptureSnapshot(std::optional<UnitId> retainedVictim,
            Snapshot& outSnapshot) const = 0;
        [[nodiscard]] virtual bool MakeStopIntent(UnitId victim, std::uint32_t epoch,
            commands::ClickedMissionIntent& outIntent) const = 0;
        [[nodiscard]] virtual bool MakeAttackIntent(UnitId hunter, UnitId victim,
            std::uint32_t epoch, commands::ClickedMissionIntent& outIntent) const = 0;
        [[nodiscard]] virtual bool DeselectAndUngroup(UnitId victim) const = 0;
    };
}
