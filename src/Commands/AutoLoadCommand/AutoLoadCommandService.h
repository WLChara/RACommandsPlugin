#pragma once

#include "Commands/AutoLoadCommand/IAutoLoadGamePort.h"
#include "ClickedMission/ClickedMissionDispatcher.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace ra_commands::autoload
{
    /**
     * 只负责将自动装车配对转成 Enter 意图，交给共用调度器。
     * 仅在游戏线程使用，不拥有游戏适配器或调度器。
     */
    class AutoLoadCommandService final
    {
    public:
        AutoLoadCommandService(IAutoLoadGamePort& game,
            commands::ClickedMissionDispatcher& dispatcher,
            const std::atomic<bool>& isSafeModeEnabled);

        // 入队成功后立即取消实际参与单位的选择；待发意图之后仍可能到期或失效。
        void OnHotkey();
        void Reset();

    private:
        struct ReservedUnit
        {
            UnitId Id = 0;
            std::uintptr_t Address = 0;
        };

        struct LoadReservation
        {
            ReservedUnit Transport;
            std::vector<ReservedUnit> Passengers;
            std::uint64_t StartedAtMs = 0;
        };

        void PruneReservations(const Snapshot& snapshot, std::uint64_t nowMs);

        IAutoLoadGamePort& mGame;
        commands::ClickedMissionDispatcher& mDispatcher;
        const std::atomic<bool>& mIsSafeModeEnabled;
        std::vector<LoadReservation> mReservations;
        std::uint32_t mEpoch = 0;
    };
}
