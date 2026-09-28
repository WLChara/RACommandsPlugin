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

    /** 一次性热键列队；跨帧状态只保存值 ID 与格子，不保存游戏对象指针。 */
    class AutoFormationCommandService final
    {
    public:
        explicit AutoFormationCommandService(IAutoFormationGamePort& game);

        [[nodiscard]] PlanResult OnHotkey();
        /** 由游戏线程主帧调用。 */
        void OnGameFrame();
        void OnManualOrder(ActorId actorId);
        void Reset();
        [[nodiscard]] bool IsActive() const;
        [[nodiscard]] ProgressSnapshot Progress() const;

    private:
        struct ActorState
        {
            Cell mDestination;
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
