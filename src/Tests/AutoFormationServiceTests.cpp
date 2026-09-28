#include "Commands/AutoFormationCommand/AutoFormationCommandService.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    using namespace ra_commands::auto_formation;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    class FakeGame final : public IAutoFormationGamePort
    {
    public:
        bool mSessionActive = true;
        bool mCaptureSucceeds = true;
        bool mRejectSubmissions = false;
        bool mSimulateGrid = false;
        std::uint32_t mSessionEpoch = 1;
        std::uint32_t mFrame = 100;
        std::vector<ActorId> mSelected{ 1 };
        std::map<ActorId, Cell> mCells{ { 1, { 0, 0, false } } };
        std::map<ActorId, bool> mAlive;
        std::map<ActorId, MoveState> mStates;
        std::map<ActorId, Cell> mDestinations;
        std::map<ActorId, std::vector<Cell>> mCandidatesByActor;
        std::set<ActorId> mAuthorized;
        std::vector<ActorId> mBegunActors;
        std::vector<Assignment> mSubmitted;
        std::vector<ActorId> mCancelled;
        std::vector<std::vector<ActorId>> mCapturedIds;
        std::uint32_t mResetCount = 0;

        bool IsSessionActive() const override { return mSessionActive; }
        std::uint32_t Epoch() const override { return mSessionEpoch; }
        std::uint32_t CurrentFrame() const override { return mFrame; }
        std::vector<ActorId> CaptureSelectedActorIds() const override { return mSelected; }

        bool TryCaptureSnapshot(const std::vector<ActorId>& ids, std::optional<Center> fixedCenter,
            Snapshot& outSnapshot) const override
        {
            auto* self = const_cast<FakeGame*>(this);
            self->mCapturedIds.push_back(ids);
            if (!mCaptureSucceeds)
            {
                return false;
            }
            Snapshot snapshot;
            if (fixedCenter)
            {
                snapshot.mCenter = *fixedCenter;
            }
            for (const auto actor : ids)
            {
                const auto cell = mCells.find(actor);
                if (cell == mCells.end() || IsDead(actor))
                {
                    continue;
                }
                ActorSnapshot value;
                value.mId = actor;
                value.mCurrentCell = cell->second;
                value.mPosition = { static_cast<double>(cell->second.mX), static_cast<double>(cell->second.mY) };
                const auto candidates = mCandidatesByActor.find(actor);
                if (candidates != mCandidatesByActor.end())
                {
                    value.mCandidates = candidates->second;
                }
                else
                {
                    for (std::int32_t x = 0; x < 8; ++x)
                    {
                        for (std::int32_t y = 0; y < 8; ++y)
                        {
                            value.mCandidates.push_back({ x, y, false });
                        }
                    }
                }
                snapshot.mActors.push_back(std::move(value));
            }
            if (!fixedCenter)
            {
                snapshot.mCenter = CalculateCenter(snapshot.mActors);
            }
            outSnapshot = std::move(snapshot);
            return true;
        }

        void BeginPlan(const std::vector<ActorId>& assignedActors) override
        {
            mAuthorized = { assignedActors.begin(), assignedActors.end() };
            mBegunActors = assignedActors;
        }

        bool TryGetActorCell(ActorId actor, Cell& outCell) const override
        {
            const auto found = mCells.find(actor);
            if (found == mCells.end() || IsDead(actor))
            {
                return false;
            }
            outCell = found->second;
            return true;
        }

        bool SubmitMove(ActorId actor, Cell destination) override
        {
            mSubmitted.push_back({ actor, destination });
            if (mRejectSubmissions || !mAuthorized.contains(actor) || IsDead(actor))
            {
                return false;
            }
            mDestinations[actor] = destination;
            mStates[actor] = MoveState::Queued;
            return true;
        }

        MoveState ObserveMove(ActorId actor, Cell) const override
        {
            if (!mAuthorized.contains(actor) || IsDead(actor))
            {
                return MoveState::Invalid;
            }
            const auto found = mStates.find(actor);
            return found == mStates.end() ? MoveState::Waiting : found->second;
        }

        void CancelPending(ActorId actor) override
        {
            mCancelled.push_back(actor);
            mAuthorized.erase(actor);
            mDestinations.erase(actor);
        }

        void Reset() override
        {
            ++mResetCount;
            mAuthorized.clear();
            mDestinations.clear();
        }

        void AdvanceGrid()
        {
            if (!mSimulateGrid)
            {
                return;
            }
            // 仅模拟目标格被队友占用后的腾格波次，不代表游戏原生寻路物理。
            std::set<std::pair<int, int>> occupied;
            for (const auto actor : mBegunActors)
            {
                if (!IsDead(actor))
                {
                    const auto cell = mCells.at(actor);
                    occupied.emplace(cell.mX, cell.mY);
                }
            }
            bool movedActor = false;
            for (const auto actor : mBegunActors)
            {
                const auto order = mDestinations.find(actor);
                if (order == mDestinations.end() || IsDead(actor))
                {
                    continue;
                }
                const auto startCell = mCells.at(actor);
                const auto goal = std::pair{ static_cast<int>(order->second.mX), static_cast<int>(order->second.mY) };
                if (startCell == order->second)
                {
                    mStates[actor] = MoveState::Arrived;
                    continue;
                }
                if (occupied.contains(goal))
                {
                    mStates[actor] = MoveState::Waiting;
                    continue;
                }
                occupied.erase({ startCell.mX, startCell.mY });
                occupied.insert(goal);
                mCells[actor] = order->second;
                mStates[actor] = MoveState::Arrived;
                movedActor = true;
            }
            if (!movedActor)
            {
                // 队内形成环形占位时，让一辆车先停到队形外侧，模拟原生波次腾格。
                for (const auto actor : mBegunActors)
                {
                    const auto order = mDestinations.find(actor);
                    if (order == mDestinations.end() || IsDead(actor) || mCells.at(actor) == order->second)
                    {
                        continue;
                    }
                    for (int y = 0; y < 16; ++y)
                    {
                        const std::pair staging{ 8, y };
                        if (occupied.contains(staging))
                        {
                            continue;
                        }
                        const auto current = mCells.at(actor);
                        occupied.erase({ current.mX, current.mY });
                        mCells[actor] = { staging.first, staging.second, false };
                        mStates[actor] = MoveState::Moving;
                        return;
                    }
                    break;
                }
            }
        }

    private:
        bool IsDead(ActorId actor) const
        {
            const auto found = mAlive.find(actor);
            return found != mAlive.end() && !found->second;
        }
    };

    Assignment FindAssignment(const PlanResult& result, ActorId actor)
    {
        const auto found = std::find_if(result.mAssignments.begin(), result.mAssignments.end(),
            [actor](const Assignment& assignment) { return assignment.mActor == actor; });
        Require(found != result.mAssignments.end(), "expected actor assignment was missing");
        return *found;
    }

    void TestQueuedMovesDoNotRepeatAndMovingProgressResetsStall()
    {
        FakeGame game;
        game.mCandidatesByActor[1] = { { 7, 7, false } };
        AutoFormationCommandService service(game);
        (void)service.OnHotkey();
        service.OnGameFrame();
        service.OnGameFrame();
        Require(game.mSubmitted.size() == 1, "Queued moves must not be submitted again");

        game.mStates[1] = MoveState::Moving;
        for (int step = 0; step < 30; ++step)
        {
            game.mCells[1] = { step % 7, 1, false };
            ++game.mFrame;
            service.OnGameFrame();
        }
        Require(game.mSubmitted.size() == 1, "cell progress must suppress duplicate move submissions");

        game.mCells[1] = { 3, 6, false };
        ++game.mFrame;
        service.OnGameFrame();
        for (int wait = 0; wait < 179; ++wait)
        {
            ++game.mFrame;
            service.OnGameFrame();
        }
        Require(game.mSubmitted.size() == 1, "a moving actor must receive the full stall grace interval");
        ++game.mFrame;
        service.OnGameFrame();
        Require(game.mSubmitted.size() == 2 && game.mSubmitted.back().mDestination == Cell{ 7, 7, false },
            "a stalled actor should retry the same fixed destination after 180 frames");
    }

    void TestQueueRejectionRetriesUntilDeadlineAndRetainsSummary()
    {
        FakeGame game;
        game.mRejectSubmissions = true;
        AutoFormationCommandService service(game);
        (void)service.OnHotkey();
        for (std::uint32_t elapsed = 0; elapsed < 899; ++elapsed)
        {
            service.OnGameFrame();
            ++game.mFrame;
        }
        Require(service.IsActive() && service.Progress().mRejectedSubmissions > 4,
            "queue rejection should retry beyond four attempts until the operation deadline");
        ++game.mFrame;
        service.OnGameFrame();
        const auto summary = service.Progress();
        Require(!service.IsActive() && !summary.mActive && summary.mTimedOut &&
            summary.mTotalActors == 1 && summary.mRejectedSubmissions > 4,
            "timeout should retain an observable summary after clearing active state");
        Require(game.mAuthorized.empty(), "timeout should clear the assigned-plan authorization");
    }

    void TestSixtyFourActorsReachOneFixedEightByEightFormation()
    {
        FakeGame game;
        game.mSelected.clear();
        game.mCells.clear();
        for (ActorId actor = 1; actor <= 64; ++actor)
        {
            game.mSelected.push_back(actor);
            game.mCells[actor] = { static_cast<std::int32_t>((actor - 1) % 8),
                static_cast<std::int32_t>((actor - 1) / 8), false };
        }
        game.mSimulateGrid = true;
        AutoFormationCommandService service(game);
        const auto plan = service.OnHotkey();
        Require(plan.mAssignments.size() == 64 && game.mBegunActors.size() == 64,
            "the full assigned team should be authorized together");

        std::set<std::pair<int, int>> destinations;
        for (const auto& assignment : plan.mAssignments)
        {
            destinations.emplace(assignment.mDestination.mX, assignment.mDestination.mY);
        }
        int minX = 8, minY = 8, maxX = -1, maxY = -1;
        for (const auto& [x, y] : destinations)
        {
            minX = std::min(minX, x); minY = std::min(minY, y);
            maxX = std::max(maxX, x); maxY = std::max(maxY, y);
        }
        const auto width = maxX - minX + 1;
        const auto height = maxY - minY + 1;
        Require(width == 8 && height == 8 &&
            destinations.size() == static_cast<std::size_t>(width * height),
            "the planned formation bounding square must not contain an interior hole");

        // 先把每辆车放在队友的目标格，模拟队伍内临占造成的移动波次。
        std::vector<ActorId> actors;
        std::vector<Cell> goals;
        for (ActorId actor = 1; actor <= 64; ++actor)
        {
            actors.push_back(actor);
            goals.push_back(FindAssignment(plan, actor).mDestination);
        }
        for (std::size_t index = 0; index < actors.size(); ++index)
        {
            game.mCells[actors[index]] = goals[(index + 1) % actors.size()];
        }
        std::size_t initiallyOccupiedGoals = 0;
        for (const auto& goal : goals)
        {
            if (std::find_if(game.mCells.begin(), game.mCells.end(),
                [&goal](const auto& entry) { return entry.second == goal; }) != game.mCells.end())
            {
                ++initiallyOccupiedGoals;
            }
        }
        Require(initiallyOccupiedGoals == goals.size(), "the initial wave should occupy every goal slot");

        for (int frame = 0; frame < 899 && service.IsActive(); ++frame)
        {
            game.AdvanceGrid();
            ++game.mFrame;
            service.OnGameFrame();
        }
        Require(!service.IsActive() && service.Progress().mArrived == 64,
            "all members should eventually stop at their original whole-team goals");
        for (std::size_t index = 0; index < actors.size(); ++index)
        {
            Require(game.mCells.at(actors[index]) == goals[index],
                "a participant wave must not change any actor's original assigned cell");
        }
    }

    void TestParkedBesideGoalAndArrivedScatterKeepOriginalGoal()
    {
        FakeGame game;
        game.mSelected = { 1, 2 };
        game.mCells = { { 1, { 3, 4, false } }, { 2, { 7, 7, false } } };
        game.mCandidatesByActor[1] = { { 4, 4, false } };
        game.mCandidatesByActor[2] = { { 5, 4, false } };
        AutoFormationCommandService service(game);
        const auto plan = service.OnHotkey();
        const auto originalGoal = FindAssignment(plan, 1).mDestination;
        (void)FindAssignment(plan, 2);
        service.OnGameFrame();

        // 旁格停住也不能被当成到位；稳定 180 帧后继续追最初目标。
        game.mCells[1] = { 3, 5, false };
        game.mStates[1] = MoveState::Moving;
        ++game.mFrame;
        service.OnGameFrame();
        for (int frame = 0; frame < 180; ++frame)
        {
            ++game.mFrame;
            service.OnGameFrame();
        }
        Require(game.mSubmitted.size() >= 3 && game.mSubmitted.back().mActor == 1 &&
            game.mSubmitted.back().mDestination == originalGoal,
            "a native stop beside the target should retry the original slot");

        // 标记一辆已到位，保持另一辆未完成，再模拟散开移动。
        game.mCells[1] = originalGoal;
        game.mStates[1] = MoveState::Arrived;
        game.mStates[2] = MoveState::Waiting;
        game.mFrame += 15;
        service.OnGameFrame();
        Require(service.IsActive(), "one arrived member must not complete the unfinished team");
        game.mCells[1] = { 3, 4, false };
        game.mFrame += 15;
        service.OnGameFrame();
        Require(service.IsActive() && game.mSubmitted.back().mActor == 1 &&
            game.mSubmitted.back().mDestination == originalGoal,
            "a scattered arrived member should return to its original goal without replanning the team");
    }

    void TestManualDeathAndLifecycleOnlyRemoveTheirOwnMembers()
    {
        FakeGame game;
        game.mSelected = { 1, 2, 3 };
        game.mCells = { { 1, { 0, 0, false } }, { 2, { 7, 0, false } }, { 3, { 0, 7, false } } };
        AutoFormationCommandService service(game);
        (void)service.OnHotkey();
        service.OnManualOrder(1);
        game.mAlive[2] = false;
        ++game.mFrame;
        service.OnGameFrame();
        Require(service.IsActive() && game.mAuthorized == std::set<ActorId>{ 3 } &&
            service.Progress().mRemoved == 2,
            "manual order and death should remove only their actors and preserve the rest of the plan");

        game.mSelected = { 4 };
        game.mCells[4] = { 2, 2, false };
        (void)service.OnHotkey();
        Require(game.mAuthorized == std::set<ActorId>{ 4 } && service.Progress().mTotalActors == 1,
            "a new hotkey should replace the old assigned set and reset its progress summary");
        game.mSessionEpoch += 1;
        service.OnGameFrame();
        Require(!service.IsActive() && game.mAuthorized.empty(), "epoch change should clear plan authorization");

        (void)service.OnHotkey();
        game.mFrame -= 1;
        service.OnGameFrame();
        Require(!service.IsActive() && !service.Progress().mTimedOut,
            "frame rollback should clear authorization without misreporting a timeout");
    }
}

void RunAutoFormationServiceTests()
{
    TestQueuedMovesDoNotRepeatAndMovingProgressResetsStall();
    TestQueueRejectionRetriesUntilDeadlineAndRetainsSummary();
    TestSixtyFourActorsReachOneFixedEightByEightFormation();
    TestParkedBesideGoalAndArrivedScatterKeepOriginalGoal();
    TestManualDeathAndLifecycleOnlyRemoveTheirOwnMembers();
}
