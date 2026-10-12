#include "Commands/AutoFormationCommand/AutoFormationCommandService.h"
#include "Commands/AutoFormationCommand/FormationCellPacking.h"

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
        // 与既有规划器的参与者预算及值型地图坐标范围保持一致。
        constexpr std::size_t MAX_PLAN_ACTORS = 256;
        constexpr std::int32_t MAP_AXIS_LIMIT = 512;

        bool HasCompatibleTraits(const Assignment& assignment,
            const ActorObservation& observation)
        {
            return observation.mKind == assignment.mKind &&
                observation.mPlane == assignment.mPlane &&
                (assignment.mPlane == OccupancyPlane::Air ?
                    !observation.mCell.mOnBridge && !assignment.mDestination.mOnBridge :
                    observation.mCell.mOnBridge == assignment.mDestination.mOnBridge);
        }
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
        (void)StartPlan(result);
        return result;
    }

    bool AutoFormationCommandService::StartPlan(const PlanResult& plan)
    {
        if (!mGame.IsSessionActive() || plan.mBudgetExceeded ||
            plan.mAssignments.empty() || plan.mAssignments.size() > MAX_PLAN_ACTORS ||
            !HasValidReservations(plan.mAssignments))
        {
            return false;
        }

        std::map<ActorId, ActorState> actors;
        for (const auto& assignment : plan.mAssignments)
        {
            ActorObservation observation;
            const auto cell = assignment.mDestination;
            if (assignment.mActor == 0 || cell.mX < 0 || cell.mX >= MAP_AXIS_LIMIT ||
                cell.mY < 0 || cell.mY >= MAP_AXIS_LIMIT ||
                !mGame.TryGetActorState(assignment.mActor, observation) ||
                !HasCompatibleTraits(assignment, observation))
            {
                return false;
            }
            ActorState state;
            state.mAssignment = assignment;
            state.mLastCell = observation.mCell;
            state.mProgressFrame = mGame.CurrentFrame();
            state.mHasProgressFrame = true;
            if (!actors.try_emplace(assignment.mActor, state).second) { return false; }
        }
        Reset();
        mProgress = {};
        mActors = std::move(actors);
        mGame.BeginPlan(plan.mAssignments);
        mEpoch = mGame.Epoch();
        mStartFrame = mGame.CurrentFrame();
        mLastFrame = mStartFrame;
        mHasFrame = true;
        mActive = true;
        mProgress.mActive = true;
        mProgress.mTotalActors = mActors.size();
        return true;
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
            ActorObservation observation;
            if (!mGame.TryGetActorState(actor, observation) ||
                !HasCompatibleTraits(state.mAssignment, observation))
            {
                removedActors.push_back(actor);
                continue;
            }
            const auto currentCell = observation.mCell;
            if (!state.mHasProgressFrame || currentCell != state.mLastCell)
            {
                state.mLastCell = currentCell;
                state.mProgressFrame = frame;
                state.mHasProgressFrame = true;
            }

            const auto moveState = mGame.ObserveMove(state.mAssignment);
            if (moveState == MoveState::Invalid || moveState == MoveState::Overridden)
            {
                removedActors.push_back(actor);
                continue;
            }

            const bool arrived = moveState == MoveState::Arrived &&
                currentCell == state.mAssignment.mDestination;
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
            if (mGame.SubmitMove(state.mAssignment))
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
