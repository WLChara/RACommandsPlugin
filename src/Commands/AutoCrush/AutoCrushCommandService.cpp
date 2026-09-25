#include "Commands/AutoCrush/AutoCrushCommandService.h"

namespace ra_commands::auto_crush
{
    namespace
    {
        constexpr std::uint32_t PLAN_INTERVAL_FRAMES = 15;
        // 行驶中 45 帧内不转向；相同终点至少 90 帧后才重试。
        constexpr std::uint32_t MIN_ROUTE_CHANGE_FRAMES = 45;
        constexpr std::uint32_t STALLED_RETRY_FRAMES = 90;
        constexpr std::size_t MAX_MARKED_CRUSHERS = 128;
    }

    AutoCrushCommandService::AutoCrushCommandService(IAutoCrushGamePort& game)
        : mGame(game)
    {
    }

    void AutoCrushCommandService::OnAddHotkey()
    {
        if (!mGame.IsSessionActive())
        {
            return;
        }
        if (mEpoch != mGame.Epoch())
        {
            Reset();
            mEpoch = mGame.Epoch();
        }
        for (const auto id : mGame.CaptureSelectedEligibleCrushers())
        {
            if (mMarked.size() >= MAX_MARKED_CRUSHERS)
            {
                break;
            }
            mMarked.try_emplace(id);
        }
    }

    void AutoCrushCommandService::Remove(const std::vector<CrusherId>& ids)
    {
        for (const auto id : ids)
        {
            if (mMarked.erase(id) != 0)
            {
                mGame.CancelPending(id);
            }
        }
    }

    void AutoCrushCommandService::OnRemoveHotkey()
    {
        if (mGame.IsSessionActive() && mEpoch == mGame.Epoch())
        {
            Remove(mGame.CaptureSelectedVehicleIds());
        }
    }

    void AutoCrushCommandService::OnManualOrder(CrusherId actorId)
    {
        if (mGame.IsSessionActive() && mEpoch == mGame.Epoch())
        {
            if (mMarked.erase(actorId) != 0)
            {
                mGame.CancelPending(actorId);
            }
        }
    }

    void AutoCrushCommandService::OnGameFrame()
    {
        if (!mGame.IsSessionActive())
        {
            Reset();
            return;
        }
        if (mEpoch != mGame.Epoch())
        {
            Reset();
            mEpoch = mGame.Epoch();
        }
        for (auto it = mMarked.begin(); it != mMarked.end();)
        {
            if (mGame.IsEligibleCrusher(it->first))
            {
                ++it;
                continue;
            }
            mGame.CancelPending(it->first);
            it = mMarked.erase(it);
        }
        if (mMarked.empty())
        {
            return;
        }

        const auto frame = mGame.CurrentFrame();
        if (mHasPlanned && frame - mLastPlanFrame < PLAN_INTERVAL_FRAMES)
        {
            return;
        }
        mLastPlanFrame = frame;
        mHasPlanned = true;

        std::vector<CrusherId> ids;
        ids.reserve(mMarked.size());
        for (const auto& [id, state] : mMarked)
        {
            ids.push_back(id);
        }
        Snapshot snapshot;
        if (!mGame.TryCaptureSnapshot(ids, snapshot))
        {
            return;
        }
        std::map<CrusherId, Cell> currentCells;
        for (const auto& crusher : snapshot.mCrushers)
        {
            currentCells.try_emplace(crusher.mId, crusher.mCurrentCell);
        }
        const auto plan = Plan(snapshot);
        for (const auto& move : plan.mMoves)
        {
            const auto stateIt = mMarked.find(move.mCrusherId);
            if (stateIt == mMarked.end())
            {
                continue;
            }
            auto& state = stateIt->second;
            const auto currentCell = currentCells.find(move.mCrusherId);
            if (currentCell == currentCells.end())
            {
                continue;
            }
            if (state.HasLastDestination &&
                currentCell->second != state.LastDestination)
            {
                const auto minInterval = move.mDestination == state.LastDestination
                    ? STALLED_RETRY_FRAMES : MIN_ROUTE_CHANGE_FRAMES;
                if (frame - state.LastSentFrame < minInterval)
                {
                    continue;
                }
            }
            if (mGame.SubmitMove(move.mCrusherId, move.mDestination))
            {
                state.LastDestination = move.mDestination;
                state.LastSentFrame = frame;
                state.HasLastDestination = true;
            }
        }
    }

    void AutoCrushCommandService::Reset()
    {
        for (const auto& [id, state] : mMarked)
        {
            mGame.CancelPending(id);
        }
        mMarked.clear();
        mEpoch = 0;
        mLastPlanFrame = 0;
        mHasPlanned = false;
    }

    std::size_t AutoCrushCommandService::MarkedCount() const noexcept
    {
        return mMarked.size();
    }
}
