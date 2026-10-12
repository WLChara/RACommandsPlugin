#include "Commands/DragFormationCommand/DragFormationCommandService.h"

#include "Commands/DragFormationCommand/DragFormationPlanner.h"
#include "Commands/AutoFormationCommand/FormationCellPacking.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <unordered_set>
#include <utility>

namespace ra_commands::drag_formation
{
    namespace
    {
        constexpr std::uint64_t CHORD_HOLD_MS = 500;
        // 与演示版布局参数一致，容量回退只改变每格人数，不改变槽位节距。
        constexpr double DEMO_RADIUS = 0.4;
        constexpr double DEMO_SPACING = 0.3;
        constexpr unsigned LEFT_BUTTON_MASK = 1u;
        constexpr unsigned RIGHT_BUTTON_MASK = 2u;
        constexpr unsigned BOTH_BUTTONS_MASK = LEFT_BUTTON_MASK | RIGHT_BUTTON_MASK;

        unsigned ButtonMask(PointerButton button)
        {
            switch (button)
            {
            case PointerButton::Left:
                return LEFT_BUTTON_MASK;
            case PointerButton::Right:
                return RIGHT_BUTTON_MASK;
            default:
                return 0;
            }
        }

        bool IsFinite(WorldPoint point)
        {
            return std::isfinite(point.mX) && std::isfinite(point.mY);
        }

        bool IsValidProfile(const auto_formation::ActorProfile& profile)
        {
            using auto_formation::ActorKind;
            using auto_formation::OccupancyPlane;
            return profile.mId != 0 &&
                (profile.mKind == ActorKind::Vehicle || profile.mKind == ActorKind::Infantry) &&
                (profile.mPlane == OccupancyPlane::Ground ||
                    profile.mPlane == OccupancyPlane::Air) &&
                (profile.mPlane != OccupancyPlane::Air || !profile.mOnBridge);
        }

        bool SnapshotMatchesProfiles(const auto_formation::Snapshot& snapshot,
            const std::vector<auto_formation::ActorProfile>& profiles)
        {
            if (snapshot.mActors.size() != profiles.size())
            {
                return false;
            }
            std::map<auto_formation::ActorId, auto_formation::ActorProfile> expected;
            for (const auto& profile : profiles)
            {
                expected.emplace(profile.mId, profile);
            }
            for (const auto& actor : snapshot.mActors)
            {
                const auto found = expected.find(actor.mId);
                if (found == expected.end() || actor.mKind != found->second.mKind ||
                    actor.mPlane != found->second.mPlane ||
                    actor.mCurrentCell.mOnBridge != found->second.mOnBridge)
                {
                    return false;
                }
                expected.erase(found);
            }
            return expected.empty();
        }

        bool IsCompletePlan(const auto_formation::PlanResult& plan,
            const std::vector<auto_formation::ActorProfile>& profiles)
        {
            if (plan.mBudgetExceeded || !plan.mUnassignedActors.empty() ||
                plan.mAssignments.size() != profiles.size() ||
                !auto_formation::HasValidReservations(plan.mAssignments))
            {
                return false;
            }
            std::map<auto_formation::ActorId, auto_formation::ActorProfile> selected;
            for (const auto& profile : profiles)
            {
                selected.emplace(profile.mId, profile);
            }
            std::unordered_set<auto_formation::ActorId> assigned;
            for (const auto& assignment : plan.mAssignments)
            {
                const auto found = selected.find(assignment.mActor);
                if (found == selected.end() ||
                    assignment.mKind != found->second.mKind ||
                    assignment.mPlane != found->second.mPlane ||
                    assignment.mDestination.mOnBridge != found->second.mOnBridge ||
                    !assigned.insert(assignment.mActor).second)
                {
                    return false;
                }
            }
            return assigned.size() == selected.size();
        }
    }

    DragFormationCommandService::DragFormationCommandService(IDragFormationGamePort& game)
        : mGame(game)
    {
    }

    void DragFormationCommandService::OnHotkey()
    {
        if (mEnabled)
        {
            Reset();
            return;
        }
        if (!mGame.IsSessionActive())
        {
            return;
        }
        auto actors = mGame.CaptureSelectedActorProfiles();
        if (actors.empty() || actors.size() > MAX_DEMO_ACTORS)
        {
            return;
        }
        std::unordered_set<auto_formation::ActorId> uniqueActors;
        for (const auto& actor : actors)
        {
            if (!IsValidProfile(actor) || !uniqueActors.insert(actor.mId).second)
            {
                return;
            }
        }
        mEpoch = mGame.Epoch();
        mActors = std::move(actors);
        mEnabled = true;
        mOutcome = Outcome::Armed;
    }

    void DragFormationCommandService::OnPointerEvent(const PointerEvent& event)
    {
        if (event.mKind == PointerEventKind::Cancel)
        {
            Reset();
            return;
        }
        if (!mEnabled)
        {
            return;
        }
        if (!mGame.IsSessionActive() || mGame.Epoch() != mEpoch)
        {
            Reset();
            return;
        }

        const auto mask = ButtonMask(event.mButton);
        switch (event.mKind)
        {
        case PointerEventKind::Down:
            if (mask == 0 || (mButtons & mask) != 0)
            {
                return;
            }
            mButtons |= mask;
            if (mButtons == BOTH_BUTTONS_MASK)
            {
                mHasChord = event.mPosition && IsFinite(*event.mPosition);
                mPointerValid = mHasChord;
                mChordStartMs = event.mTimeMs;
                if (mHasChord)
                {
                    mAnchor = *event.mPosition;
                    mEnd = mAnchor;
                }
            }
            break;
        case PointerEventKind::Move:
            if (!mHasChord || mButtons != BOTH_BUTTONS_MASK)
            {
                return;
            }
            mPointerValid = event.mPosition && IsFinite(*event.mPosition);
            if (mPointerValid)
            {
                mEnd = *event.mPosition;
            }
            UpdatePreview(event.mTimeMs);
            break;
        case PointerEventKind::Up:
            if (mask == 0 || (mButtons & mask) == 0)
            {
                return;
            }
            if (mHasChord && mButtons == BOTH_BUTTONS_MASK)
            {
                mPointerValid = event.mPosition && IsFinite(*event.mPosition);
                if (mPointerValid)
                {
                    mEnd = *event.mPosition;
                }
                if (event.mTimeMs >= mChordStartMs &&
                    event.mTimeMs - mChordStartMs >= CHORD_HOLD_MS)
                {
                    Finish(event.mTimeMs);
                    return;
                }
                // 短按不消耗一次性模式，释放双键后可以重新尝试。
                mHasChord = false;
                mPointerValid = false;
                mPreview = {};
            }
            mButtons &= ~mask;
            break;
        case PointerEventKind::Cancel:
            break;
        }
    }

    void DragFormationCommandService::OnGameFrame(std::uint64_t timeMs)
    {
        if (!mEnabled)
        {
            return;
        }
        if (!mGame.IsSessionActive() || mGame.Epoch() != mEpoch)
        {
            Reset();
            return;
        }
        UpdatePreview(timeMs);
    }

    void DragFormationCommandService::UpdatePreview(std::uint64_t timeMs)
    {
        mPreview = {};
        if (!mHasChord || mButtons != BOTH_BUTTONS_MASK || !mPointerValid ||
            timeMs < mChordStartMs || timeMs - mChordStartMs < CHORD_HOLD_MS)
        {
            return;
        }
        auto layout = BuildLayout(mAnchor, mEnd, mActors, DEMO_RADIUS, DEMO_SPACING);
        if (layout.mValid && !layout.mPlanes.empty() && layout.mProfiles == mActors)
        {
            mPreview.mVisible = true;
            mPreview.mLayout = std::move(layout);
        }
    }

    void DragFormationCommandService::Finish(std::uint64_t timeMs)
    {
        UpdatePreview(timeMs);
        auto outcome = Outcome::InvalidGesture;
        if (mPreview.mVisible)
        {
            outcome = Outcome::PlacementRejected;
            std::vector<auto_formation::ActorId> ids;
            ids.reserve(mActors.size());
            for (const auto& actor : mActors)
            {
                ids.push_back(actor.mId);
            }
            constexpr std::array<std::uint8_t, 3> CAPACITIES{3, 2, 1};
            for (const auto capacity : CAPACITIES)
            {
                if (!mGame.IsSessionActive() || mGame.Epoch() != mEpoch)
                {
                    outcome = Outcome::Cancelled;
                    break;
                }
                const auto layout = capacity == CAPACITIES.front() ? mPreview.mLayout :
                    BuildLayout(mAnchor, mEnd, mActors, DEMO_RADIUS, DEMO_SPACING, capacity);
                if (!layout.mValid || layout.mProfiles != mActors)
                {
                    continue;
                }
                const auto cells = BuildPlacementCells(layout);
                if (cells.empty() || cells.size() > MAX_PLACEMENT_CELLS)
                {
                    continue;
                }
                auto_formation::Snapshot snapshot;
                if (!mGame.TryCapturePlacementSnapshot(ids, cells, snapshot))
                {
                    continue;
                }
                // 运行时类别/层已变化时，不能把旧热键快照换成新资源池后继续重试。
                if (!SnapshotMatchesProfiles(snapshot, mActors))
                {
                    break;
                }
                const auto plan = PlanPlacement(layout, snapshot);
                if (IsCompletePlan(plan, mActors))
                {
                    outcome = mGame.Commit(plan) ?
                        Outcome::Scheduled : Outcome::ExecutionRejected;
                    break;
                }
            }
        }
        // 达到阈值后无论规划或原生入队是否成功，都结束这次演示模式。
        Reset();
        mOutcome = outcome;
    }

    void DragFormationCommandService::Reset()
    {
        if (mEnabled)
        {
            mOutcome = Outcome::Cancelled;
        }
        mActors.clear();
        mPreview = {};
        mAnchor = {};
        mEnd = {};
        mEpoch = 0;
        mChordStartMs = 0;
        mButtons = 0;
        mEnabled = false;
        mHasChord = false;
        mPointerValid = false;
    }

    bool DragFormationCommandService::IsEnabled() const
    {
        return mEnabled;
    }

    const Preview& DragFormationCommandService::GetPreview() const
    {
        return mPreview;
    }

    Outcome DragFormationCommandService::GetOutcome() const
    {
        return mOutcome;
    }
}
