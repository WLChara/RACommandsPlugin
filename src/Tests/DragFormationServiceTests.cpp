#include "Commands/DragFormationCommand/DragFormationCommandService.h"

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
    using namespace ra_commands::drag_formation;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    class FakeGame final : public IDragFormationGamePort
    {
    public:
        bool mSessionActive = true;
        bool mSnapshotSucceeds = true;
        bool mCommitSucceeds = true;
        bool mProvideCandidates = true;
        std::uint8_t mMaxAvailable = 0;
        std::uint32_t mEpoch = 1;
        std::vector<ra_commands::auto_formation::ActorId> mSelected{1};
        std::map<ra_commands::auto_formation::ActorId, ra_commands::auto_formation::ActorKind> mKinds;
        std::map<ra_commands::auto_formation::ActorId, ra_commands::auto_formation::OccupancyPlane> mPlanes;
        std::map<ra_commands::auto_formation::ActorId, bool> mBridges;
        std::map<ra_commands::auto_formation::ActorId,
            std::vector<ra_commands::auto_formation::Cell>> mCandidatesByActor;
        std::map<ra_commands::auto_formation::ActorId,
            std::vector<ra_commands::auto_formation::CandidateCapacity>> mLimitsByActor;
        mutable std::vector<ra_commands::auto_formation::ActorId> mCaptured;
        mutable int mSnapshotCalls = 0;
        int mCommitCalls = 0;
        ra_commands::auto_formation::PlanResult mLastCommitted;

        bool IsSessionActive() const override { return mSessionActive; }
        std::uint32_t Epoch() const override { return mEpoch; }
        std::vector<ra_commands::auto_formation::ActorProfile> CaptureSelectedActorProfiles() const override
        {
            std::vector<ra_commands::auto_formation::ActorProfile> profiles;
            for (const auto id : mSelected)
            {
                profiles.push_back({id, KindOf(id), PlaneOf(id), BridgeOf(id)});
            }
            return profiles;
        }

        bool TryCapturePlacementSnapshot(
            const std::vector<ra_commands::auto_formation::ActorId>& actors,
            const std::vector<ra_commands::auto_formation::Cell>& cells,
            const Layout&,
            ra_commands::auto_formation::Snapshot& outSnapshot) const override
        {
            ++mSnapshotCalls;
            mCaptured = actors;
            if (!mSnapshotSucceeds)
            {
                return false;
            }
            outSnapshot = {};
            outSnapshot.mCenter = {10.0, 10.0};
            for (const auto id : actors)
            {
                ra_commands::auto_formation::ActorSnapshot actor;
                actor.mId = id;
                actor.mPosition = {10.0 + static_cast<double>(id - 1), 10.0};
                actor.mCurrentCell = {static_cast<std::int32_t>(10 + id - 1), 10, BridgeOf(id)};
                actor.mKind = KindOf(id);
                actor.mPlane = PlaneOf(id);
                if (mProvideCandidates)
                {
                    const auto found = mCandidatesByActor.find(id);
                    actor.mCandidates = found == mCandidatesByActor.end() ? cells : found->second;
                }
                const auto limits = mLimitsByActor.find(id);
                if (limits != mLimitsByActor.end())
                {
                    actor.mCapacityLimits = limits->second;
                }
                if (mMaxAvailable != 0)
                {
                    for (const auto& cell : actor.mCandidates)
                    {
                        actor.mCapacityLimits.push_back({cell, mMaxAvailable});
                    }
                }
                outSnapshot.mActors.push_back(std::move(actor));
            }
            return true;
        }

        bool Commit(const ra_commands::auto_formation::PlanResult& plan) override
        {
            ++mCommitCalls;
            mLastCommitted = plan;
            return mCommitSucceeds;
        }

    private:
        ra_commands::auto_formation::ActorKind KindOf(
            ra_commands::auto_formation::ActorId id) const
        {
            const auto found = mKinds.find(id);
            return found == mKinds.end() ?
                ra_commands::auto_formation::ActorKind::Vehicle : found->second;
        }

        ra_commands::auto_formation::OccupancyPlane PlaneOf(
            ra_commands::auto_formation::ActorId id) const
        {
            const auto found = mPlanes.find(id);
            return found == mPlanes.end() ?
                ra_commands::auto_formation::OccupancyPlane::Ground : found->second;
        }

        bool BridgeOf(ra_commands::auto_formation::ActorId id) const
        {
            const auto found = mBridges.find(id);
            return found != mBridges.end() && found->second;
        }
    };

    PointerEvent Event(PointerEventKind kind, PointerButton button,
        std::uint64_t timeMs, std::optional<WorldPoint> point = WorldPoint{10.0, 10.0})
    {
        return {kind, button, point, timeMs};
    }

    void BeginChord(DragFormationCommandService& service,
        std::uint64_t firstMs = 100, std::uint64_t chordMs = 200)
    {
        service.OnPointerEvent(Event(PointerEventKind::Down, PointerButton::Left, firstMs));
        service.OnPointerEvent(Event(PointerEventKind::Down, PointerButton::Right, chordMs));
    }

    void TestThresholdAndTrailingButton()
    {
        FakeGame game;
        DragFormationCommandService service(game);
        service.OnHotkey();
        Require(service.GetOutcome() == Outcome::Armed,
            "arming should expose an outcome");
        BeginChord(service);
        service.OnPointerEvent(Event(PointerEventKind::Move, PointerButton::None,
            699, WorldPoint{14.0, 10.0}));
        Require(!service.GetPreview().mVisible, "499 ms must not show preview");
        service.OnGameFrame(700);
        Require(service.GetPreview().mVisible, "500 ms must show preview");
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Left,
            701, WorldPoint{14.0, 10.0}));
        Require(game.mCommitCalls == 1 && !service.IsEnabled(),
            "first release after threshold must commit once and exit");
        Require(service.GetOutcome() == Outcome::Scheduled,
            "successful commit must retain scheduled outcome after reset");
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Right, 702));
        Require(game.mCommitCalls == 1, "trailing button release must not commit again");
    }

    void TestStationaryFrameReachesThreshold()
    {
        FakeGame game;
        DragFormationCommandService service(game);
        service.OnHotkey();
        BeginChord(service);
        service.OnPointerEvent(Event(PointerEventKind::Move, PointerButton::None,
            300, WorldPoint{10.0, 16.0}));
        service.OnGameFrame(699);
        Require(!service.GetPreview().mVisible, "held chord must wait 500 ms");
        service.OnGameFrame(700);
        Require(service.GetPreview().mVisible,
            "valid dragged chord must show preview without another WM_MOUSEMOVE");
    }

    void TestAnchorStaysAtSecondButtonDown()
    {
        FakeGame game;
        DragFormationCommandService service(game);
        service.OnHotkey();
        service.OnPointerEvent(Event(PointerEventKind::Down, PointerButton::Left,
            0, WorldPoint{10.0, 10.0}));
        service.OnPointerEvent(Event(PointerEventKind::Down, PointerButton::Right,
            100, WorldPoint{20.0, 20.0}));
        service.OnPointerEvent(Event(PointerEventKind::Move, PointerButton::None,
            200, WorldPoint{20.0, 26.0}));
        service.OnGameFrame(600);
        Require(service.GetPreview().mVisible &&
            service.GetPreview().mLayout.mAnchor.mX == 20.0 &&
            service.GetPreview().mLayout.mAnchor.mY == 20.0 &&
            service.GetPreview().mLayout.mEnd.mX == 20.0 &&
            service.GetPreview().mLayout.mEnd.mY == 26.0,
            "500 ms frame must retain the second-down point as anchor");

        // 后续镜头或鼠标投影变化只更新拖拽末端，不反向改写起点。
        service.OnPointerEvent(Event(PointerEventKind::Move, PointerButton::None,
            700, WorldPoint{30.0, 31.0}));
        Require(service.GetPreview().mLayout.mAnchor.mX == 20.0 &&
            service.GetPreview().mLayout.mAnchor.mY == 20.0 &&
            service.GetPreview().mLayout.mEnd.mX == 30.0 &&
            service.GetPreview().mLayout.mEnd.mY == 31.0,
            "later pointer projection must update only the drag end");
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Left,
            701, WorldPoint{30.0, 31.0}));
        Require(game.mCommitCalls == 1 &&
            game.mLastCommitted.mCenter.mX == 20.0 &&
            game.mLastCommitted.mCenter.mY == 20.0,
            "committed plan center must remain the second-down anchor");
    }

    void TestShortReleaseAllowsRetry()
    {
        FakeGame game;
        DragFormationCommandService service(game);
        service.OnHotkey();
        BeginChord(service);
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Left,
            699, WorldPoint{14.0, 10.0}));
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Right, 700));
        Require(service.IsEnabled() && game.mCommitCalls == 0 &&
            !service.GetPreview().mVisible,
            "short chord must stay armed without committing");
        BeginChord(service, 800, 900);
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Right,
            1400, WorldPoint{14.0, 10.0}));
        Require(game.mCommitCalls == 1 && !service.IsEnabled(),
            "second valid chord must commit once");
    }

    void TestInvalidPointerAndCancel()
    {
        FakeGame game;
        DragFormationCommandService service(game);
        service.OnHotkey();
        BeginChord(service);
        service.OnPointerEvent(Event(PointerEventKind::Move, PointerButton::None,
            700, WorldPoint{14.0, 10.0}));
        Require(service.GetPreview().mVisible, "valid pointer should show preview");
        service.OnPointerEvent(Event(PointerEventKind::Move, PointerButton::None,
            701, std::nullopt));
        Require(!service.GetPreview().mVisible, "outside viewport must hide preview");
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Left,
            702, std::nullopt));
        Require(game.mCommitCalls == 0 && !service.IsEnabled(),
            "invalid final position must not commit");
        Require(service.GetOutcome() == Outcome::InvalidGesture,
            "invalid final pointer must retain invalid gesture outcome");

        service.OnHotkey();
        BeginChord(service, 800, 900);
        service.OnPointerEvent(Event(PointerEventKind::Cancel, PointerButton::None, 1000));
        Require(!service.IsEnabled() && !service.GetPreview().mVisible &&
            game.mCommitCalls == 0, "cancel must clear armed gesture");
        Require(service.GetOutcome() == Outcome::Cancelled,
            "cancel should expose a cancellation outcome");
    }

    void TestSessionEpochAndStaticActors()
    {
        FakeGame game;
        game.mSelected = {1, 2};
        DragFormationCommandService service(game);
        service.OnHotkey();
        game.mSelected = {3};
        BeginChord(service);
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Left,
            700, WorldPoint{14.0, 10.0}));
        Require(game.mCaptured == std::vector<ra_commands::auto_formation::ActorId>{1, 2},
            "actor identities must remain locked from hotkey to release");

        service.OnHotkey();
        BeginChord(service, 800, 900);
        ++game.mEpoch;
        service.OnGameFrame(1400);
        Require(!service.IsEnabled() && !service.GetPreview().mVisible,
            "session epoch change must cancel gesture");
        service.OnHotkey();
        game.mSessionActive = false;
        service.OnGameFrame(1500);
        Require(!service.IsEnabled(), "session exit must cancel gesture");
    }

    void TestBoundAndIncompletePlan()
    {
        FakeGame game;
        game.mSelected.assign(1025, 1);
        DragFormationCommandService service(game);
        service.OnHotkey();
        Require(!service.IsEnabled(), "more than 1024 actors must not arm mode");
        game.mSelected = {1, 1};
        service.OnHotkey();
        Require(!service.IsEnabled(), "duplicate actor identities must not arm mode");
        game.mSelected = {1};
        game.mPlanes[1] = ra_commands::auto_formation::OccupancyPlane::Air;
        game.mBridges[1] = true;
        service.OnHotkey();
        Require(!service.IsEnabled(), "air profile cannot reserve bridge layer");
        game.mPlanes.clear();
        game.mBridges.clear();
        game.mSelected.clear();
        for (ra_commands::auto_formation::ActorId id = 1; id <= 128; ++id)
        {
            game.mSelected.push_back(id);
        }
        service.OnHotkey();
        Require(service.IsEnabled(), "exactly 128 distinct actors may arm mode");
        service.OnHotkey();
        Require(!service.IsEnabled(), "second hotkey must cancel one-shot mode");
        game.mSelected = {1, 2};
        game.mProvideCandidates = false;
        service.OnHotkey();
        BeginChord(service);
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Left,
            700, WorldPoint{14.0, 10.0}));
        Require(game.mCommitCalls == 0 && !service.IsEnabled(),
            "incomplete placement must not submit a partial team");
        Require(service.GetOutcome() == Outcome::PlacementRejected,
            "incomplete placement must report rejection");
    }

    void TestCommitFailureStillExits()
    {
        FakeGame game;
        game.mCommitSucceeds = false;
        DragFormationCommandService service(game);
        service.OnHotkey();
        BeginChord(service);
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Right,
            700, WorldPoint{14.0, 10.0}));
        Require(game.mCommitCalls == 1 && !service.IsEnabled(),
            "failed native submission must still end one-shot mode");
        Require(service.GetOutcome() == Outcome::ExecutionRejected,
            "failed native submission must retain execution rejection");
    }

    void TestFrozenProfilesAndPlaneTransition()
    {
        using ra_commands::auto_formation::ActorKind;
        using ra_commands::auto_formation::OccupancyPlane;
        FakeGame game;
        game.mSelected = {1, 2};
        game.mKinds[1] = ActorKind::Infantry;
        game.mKinds[2] = ActorKind::Infantry;
        game.mPlanes[2] = OccupancyPlane::Air;
        DragFormationCommandService service(game);
        service.OnHotkey();
        game.mSelected = {3};
        BeginChord(service);
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Left,
            700, WorldPoint{14.0, 10.0}));
        Require(game.mCaptured == std::vector<ra_commands::auto_formation::ActorId>{1, 2} &&
            game.mCommitCalls == 1 &&
            ra_commands::auto_formation::HasValidReservations(game.mLastCommitted.mAssignments),
            "locked mixed profiles should commit independently of later selection");

        game.mSelected = {1, 2};
        service.OnHotkey();
        BeginChord(service, 800, 900);
        game.mPlanes[2] = OccupancyPlane::Ground;
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Left,
            1400, WorldPoint{14.0, 10.0}));
        Require(game.mCommitCalls == 1 && game.mSnapshotCalls == 2 &&
            service.GetOutcome() == Outcome::PlacementRejected,
            "plane transition must reject frozen profiles without retrying another capacity");
    }

    void TestInfantryCapacityFallback()
    {
        using ra_commands::auto_formation::ActorKind;
        FakeGame threeSeatGame;
        threeSeatGame.mSelected = {1, 2, 3, 4};
        for (const auto actor : threeSeatGame.mSelected)
        {
            threeSeatGame.mKinds[actor] = ActorKind::Infantry;
        }
        DragFormationCommandService threeSeatService(threeSeatGame);
        threeSeatService.OnHotkey();
        BeginChord(threeSeatService);
        threeSeatService.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Left,
            700, WorldPoint{14.0, 10.0}));
        Require(threeSeatGame.mCommitCalls == 1 && threeSeatGame.mSnapshotCalls == 1 &&
            threeSeatGame.mLastCommitted.mAssignments.size() == 4,
            "four infantry should use the default three-seat layout");
        std::map<std::pair<int, int>, std::set<unsigned>> threeSeatCells;
        for (const auto& assignment : threeSeatGame.mLastCommitted.mAssignments)
        {
            threeSeatCells[{assignment.mDestination.mX,
                assignment.mDestination.mY}].insert(assignment.mSeat);
        }
        Require(threeSeatCells.size() == 2 &&
            std::any_of(threeSeatCells.begin(), threeSeatCells.end(),
                [](const auto& entry) { return entry.second == std::set<unsigned>{0, 1, 2}; }),
            "default four-infantry layout must pack three logical seats in one cell");

        FakeGame game;
        game.mSelected = {1, 2, 3, 4};
        for (const auto actor : game.mSelected)
        {
            game.mKinds[actor] = ActorKind::Infantry;
        }
        game.mMaxAvailable = 2;
        DragFormationCommandService service(game);
        service.OnHotkey();
        BeginChord(service);
        service.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Left,
            700, WorldPoint{14.0, 10.0}));
        Require(game.mCommitCalls == 1 && game.mSnapshotCalls >= 2 &&
            game.mLastCommitted.mAssignments.size() == 4,
            "three-seat layout should retry as two-seat bundles when only two places remain");
        std::set<std::pair<int, int>> cells;
        for (const auto& assignment : game.mLastCommitted.mAssignments)
        {
            Require(assignment.mKind == ActorKind::Infantry && assignment.mSeat < 2,
                "two-seat fallback must not use a third infantry seat");
            cells.emplace(assignment.mDestination.mX, assignment.mDestination.mY);
        }
        Require(cells.size() == 2 &&
            ra_commands::auto_formation::HasValidReservations(game.mLastCommitted.mAssignments),
            "four infantry should occupy two valid physical cells after fallback");

        FakeGame oneSeatGame;
        oneSeatGame.mSelected = {1, 2, 3};
        oneSeatGame.mMaxAvailable = 1;
        for (const auto actor : oneSeatGame.mSelected)
        {
            oneSeatGame.mKinds[actor] = ActorKind::Infantry;
        }
        DragFormationCommandService oneSeatService(oneSeatGame);
        oneSeatService.OnHotkey();
        BeginChord(oneSeatService);
        oneSeatService.OnPointerEvent(Event(PointerEventKind::Up, PointerButton::Left,
            700, WorldPoint{14.0, 10.0}));
        Require(oneSeatGame.mCommitCalls == 1 && oneSeatGame.mSnapshotCalls == 3 &&
            oneSeatGame.mLastCommitted.mAssignments.size() == 3,
            "one-seat capacity should succeed only on third layout attempt");
        std::set<std::pair<int, int>> oneSeatCells;
        for (const auto& assignment : oneSeatGame.mLastCommitted.mAssignments)
        {
            Require(assignment.mSeat == 0, "one-seat fallback must only use seat zero");
            oneSeatCells.emplace(assignment.mDestination.mX, assignment.mDestination.mY);
        }
        Require(oneSeatCells.size() == 3,
            "one-seat fallback must assign three distinct physical cells");
    }
}

void RunDragFormationServiceTests()
{
    TestThresholdAndTrailingButton();
    TestStationaryFrameReachesThreshold();
    TestAnchorStaysAtSecondButtonDown();
    TestShortReleaseAllowsRetry();
    TestInvalidPointerAndCancel();
    TestSessionEpochAndStaticActors();
    TestBoundAndIncompletePlan();
    TestCommitFailureStillExits();
    TestFrozenProfilesAndPlaneTransition();
    TestInfantryCapacityFallback();
}
