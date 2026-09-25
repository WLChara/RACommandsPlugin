#pragma once

#include "ClickedMission/ClickedMissionQueue.h"
#include "ClickedMission/IClickedMissionGamePort.h"

#include <cstdint>

namespace ra_commands::commands
{
    /**
     * 单一游戏线程调度器，供多个 Command 共用原生队列背压、期限和 FIFO 顺序。
     * 不持有游戏适配器；离开对局或帧计数回退时废弃全部旧意图。
     */
    class ClickedMissionDispatcher final
    {
    public:
        explicit ClickedMissionDispatcher(IClickedMissionGamePort& game);

        void OnGameFrame();
        ClickedMissionEnqueueResult Submit(const ClickedMissionIntent& intent);
        std::size_t CancelByProducer(ClickedMissionProducer producer);
        std::size_t CancelByProducerAndActor(ClickedMissionProducer producer,
            const ClickedMissionIdentity& actor);
        [[nodiscard]] bool HasPendingActor(ClickedMissionProducer producer,
            std::uintptr_t address, std::uint32_t uniqueId) const noexcept;
        void Reset();

        [[nodiscard]] bool IsSessionActive() const noexcept;
        [[nodiscard]] std::uint32_t Epoch() const noexcept;
        [[nodiscard]] const ClickedMissionQueueCounters& Counters() const noexcept;

    private:
        IClickedMissionGamePort& mGame;
        ClickedMissionQueue mQueue;
        std::uintptr_t mSessionIdentity = 0;
        std::uint32_t mEpoch = 1;
        std::uint32_t mLastFrame = 0;
        bool mIsSessionActive = false;
    };
}
