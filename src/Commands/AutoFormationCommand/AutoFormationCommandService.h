#pragma once

#include "Commands/AutoFormationCommand/IAutoFormationGamePort.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace ra_commands::auto_formation
{
    struct ProgressSnapshot
    {
        bool mActive = false;
        std::size_t mTotalActors = 0;
        std::size_t mArrived = 0;
        std::size_t mQueued = 0;
        std::size_t mMoving = 0;
        std::size_t mWaiting = 0;
        std::size_t mRemoved = 0;
        std::size_t mSubmitted = 0;
        std::size_t mRejectedSubmissions = 0;
        bool mTimedOut = false;
    };

    /** 一次性热键列队；跨帧仅保存值型身份、目标及进度，不保存游戏对象指针。 */
    class AutoFormationCommandService final
    {
    public:
        explicit AutoFormationCommandService(IAutoFormationGamePort& game);

        [[nodiscard]] PlanResult OnHotkey();
        /**
         * 接收值型目标；所有assignment身份、层与席位均有效后才替换当前队伍。
         * false 不改变当前操作；不采集选区、不重新规划、不立即发送原生任务。
         */
        [[nodiscard]] bool StartPlan(const PlanResult& plan);
        /** 由游戏线程主帧调用。 */
        void OnGameFrame();
        void OnManualOrder(ActorId actorId);
        void Reset();
        [[nodiscard]] bool IsActive() const;
        [[nodiscard]] ProgressSnapshot Progress() const;

    private:
        struct ActorState
        {
            Assignment mAssignment;
            Cell mLastCell;
            std::uint32_t mLastAttemptFrame = 0;
            std::uint32_t mProgressFrame = 0;
            bool mHasAttemptFrame = false;
            bool mHasProgressFrame = false;
        };

        void CancelActor(ActorId actorId);

        IAutoFormationGamePort& mGame;
        std::map<ActorId, ActorState> mActors;
        ProgressSnapshot mProgress;
        std::uint32_t mEpoch = 0;
        std::uint32_t mStartFrame = 0;
        std::uint32_t mLastFrame = 0;
        bool mActive = false;
        bool mHasFrame = false;
    };
}
