#include "Commands/AutoFormationCommand/AutoFormationCommandService.h"

#include <utility>
#include <vector>

namespace ra_commands::auto_formation
{
    namespace
    {
        // 900 帧限定一次列队操作的最长生命周期，避免长期保留本队授权。
        constexpr std::uint32_t OPERATION_DEADLINE_FRAMES = 900;
        // 15 帧节流限制主帧重复提交，同时允许队列拥塞后及时重试。
        constexpr std::uint32_t RETRY_INTERVAL_FRAMES = 15;
        // 仅对持续 180 帧没有换格的移动单位重试，给原生寻路留出时间。
        constexpr std::uint32_t MOVING_STALL_FRAMES = 180;
    }

    AutoFormationCommandService::AutoFormationCommandService(IAutoFormationGamePort& game)
        : mGame(game)
    {
    }

    PlanResult AutoFormationCommandService::OnHotkey()
    {
        Reset();
        mProgress = {};
        PlanResult result;
        if (!mGame.IsSessionActive())
        {
            return result;
        }

        const auto selectedActors = mGame.CaptureSelectedActorIds();
        Snapshot snapshot;
        if (selectedActors.empty() ||
            !mGame.TryCaptureSnapshot(selectedActors, std::nullopt, snapshot))
        {
            return result;
        }
        result = Plan(snapshot);
        if (result.mBudgetExceeded || result.mAssignments.empty())
        {
            return result;
        }

        std::vector<ActorId> assignedActors;
        assignedActors.reserve(result.mAssignments.size());
        for (const auto& assignment : result.mAssignments)
        {
            Cell currentCell;
            if (!mGame.TryGetActorCell(assignment.mActor, currentCell))
            {
                continue;
            }
            ActorState state;
            state.mDestination = assignment.mDestination;
            state.mLastCell = currentCell;
            state.mProgressFrame = mGame.CurrentFrame();
            state.mHasProgressFrame = true;
            mActors.try_emplace(assignment.mActor, state);
            assignedActors.push_back(assignment.mActor);
        }
        if (mActors.empty())
        {
            return result;
        }

        mGame.BeginPlan(assignedActors);
        mEpoch = mGame.Epoch();
        mStartFrame = mGame.CurrentFrame();
        mLastFrame = mStartFrame;
        mHasFrame = true;
        mActive = true;
        mProgress.mActive = true;
        mProgress.mTotalActors = mActors.size();
        return result;
    }

    void AutoFormationCommandService::OnGameFrame()
    {
        if (!mActive)
        {
            return;
        }
        if (!mGame.IsSessionActive() || mEpoch != mGame.Epoch())
        {
            Reset();
            return;
        }

        const auto frame = mGame.CurrentFrame();
        const bool frameRolledBack = mHasFrame && frame < mLastFrame;
        const bool deadlineReached = !frameRolledBack && frame - mStartFrame >= OPERATION_DEADLINE_FRAMES;
        if (frameRolledBack || deadlineReached)
        {
            mProgress.mTimedOut = deadlineReached;
            Reset();
            return;
        }
        mLastFrame = frame;
        mHasFrame = true;
        mProgress.mArrived = 0;
        mProgress.mQueued = 0;
        mProgress.mMoving = 0;
        mProgress.mWaiting = 0;

        std::vector<ActorId> removedActors;
        bool allArrived = true;
        for (auto& [actor, state] : mActors)
        {
            Cell currentCell;
            if (!mGame.TryGetActorCell(actor, currentCell))
            {
                removedActors.push_back(actor);
                continue;
            }
            if (!state.mHasProgressFrame || currentCell != state.mLastCell)
            {
                state.mLastCell = currentCell;
                state.mProgressFrame = frame;
                state.mHasProgressFrame = true;
            }

            const auto moveState = mGame.ObserveMove(actor, state.mDestination);
            if (moveState == MoveState::Invalid || moveState == MoveState::Overridden)
            {
                removedActors.push_back(actor);
                continue;
            }

            const bool arrived = moveState == MoveState::Arrived && currentCell == state.mDestination;
            if (arrived)
            {
                ++mProgress.mArrived;
                continue;
            }
            allArrived = false;

            // 已到目标后被原生散开时，原目标仍有效，继续追踪而不移除本队成员。
            if (moveState == MoveState::Queued)
            {
                ++mProgress.mQueued;
                continue;
            }
            if (moveState == MoveState::Moving)
            {
                ++mProgress.mMoving;
            }
            else
            {
                ++mProgress.mWaiting;
            }
            const bool retryReady = !state.mHasAttemptFrame ||
                frame - state.mLastAttemptFrame >= RETRY_INTERVAL_FRAMES;
            if (!retryReady)
            {
                continue;
            }
            if (moveState == MoveState::Moving &&
                frame - state.mProgressFrame < MOVING_STALL_FRAMES)
            {
                continue;
            }

            state.mLastAttemptFrame = frame;
            state.mHasAttemptFrame = true;
            if (mGame.SubmitMove(actor, state.mDestination))
            {
                ++mProgress.mSubmitted;
            }
            else
            {
                ++mProgress.mRejectedSubmissions;
            }
        }

        for (const auto actor : removedActors)
        {
            CancelActor(actor);
        }
        if (mActors.empty())
        {
            Reset();
            return;
        }
        if (allArrived && removedActors.empty())
        {
            Reset();
        }
    }

    void AutoFormationCommandService::OnManualOrder(ActorId actorId)
    {
        CancelActor(actorId);
        if (mActors.empty() && mActive)
        {
            Reset();
        }
    }

    void AutoFormationCommandService::Reset()
    {
        for (const auto& [actor, state] : mActors)
        {
            (void)state;
            mGame.CancelPending(actor);
        }
        mGame.Reset();
        mActors.clear();
        mEpoch = 0;
        mStartFrame = 0;
        mLastFrame = 0;
        mActive = false;
        mHasFrame = false;
        mProgress.mActive = false;
    }

    bool AutoFormationCommandService::IsActive() const
    {
        return mActive;
    }

    ProgressSnapshot AutoFormationCommandService::Progress() const
    {
        return mProgress;
    }

    void AutoFormationCommandService::CancelActor(ActorId actorId)
    {
        if (mActors.erase(actorId) != 0)
        {
            ++mProgress.mRemoved;
            mGame.CancelPending(actorId);
        }
    }
}
