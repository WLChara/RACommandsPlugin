#pragma once

#include "Commands/AutoNanoCloudCommand/IAutoNanoCloudGamePort.h"
#include "ClickedMission/ClickedMissionDispatcher.h"

#include <cstdint>
#include <optional>

namespace ra_commands::auto_nano_cloud
{
    /** 一次热键只安排一名牺牲者；未击杀时允许下一次热键继续攻击同一目标。 */
    class AutoNanoCloudCommandService final
    {
    public:
        AutoNanoCloudCommandService(IAutoNanoCloudGamePort& game,
            commands::ClickedMissionDispatcher& dispatcher);

        void OnHotkey();
        void Reset();

    private:
        IAutoNanoCloudGamePort& mGame;
        commands::ClickedMissionDispatcher& mDispatcher;
        std::optional<UnitId> mRetainedVictim;
        std::uint32_t mEpoch = 0;
    };
}
