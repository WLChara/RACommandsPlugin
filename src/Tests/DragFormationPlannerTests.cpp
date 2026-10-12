#include "Commands/DragFormationCommand/DragFormationPlanner.h"
#include "Commands/AutoFormationCommand/FormationCellPacking.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    using namespace ra_commands::drag_formation;
    using ra_commands::auto_formation::ActorId;
    using ra_commands::auto_formation::ActorSnapshot;
    using ra_commands::auto_formation::ActorKind;
    using ra_commands::auto_formation::ActorProfile;
    using ra_commands::auto_formation::Assignment;
    using ra_commands::auto_formation::Cell;
    using ra_commands::auto_formation::PlanResult;
    using ra_commands::auto_formation::OccupancyPlane;
    using ra_commands::auto_formation::Snapshot;

    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    bool Near(double left, double right)
    {
        return std::abs(left - right) < 0.000001;
    }

    ActorSnapshot Actor(ActorId id, Cell current, std::vector<Cell> candidates)
    {
        return {id, current, {static_cast<double>(current.mX), static_cast<double>(current.mY)},
            std::move(candidates)};
    }

    Snapshot UniformSnapshot(const Layout& layout, const std::vector<Cell>& candidates)
    {
        Snapshot snapshot;
        for (std::size_t actor = 0; actor < layout.mSlots.size(); ++actor)
            snapshot.mActors.push_back(Actor(actor + 1, {100, 100, false}, candidates));
        return snapshot;
    }

    void RequireCompleteAndUnique(const Snapshot& snapshot, const PlanResult& result)
    {
        Require(!result.mBudgetExceeded && result.mAssignments.size() == snapshot.mActors.size() &&
            result.mUnassignedActors.empty(), "a complete plan must assign the entire selected team");
        std::vector<Cell> used;
        for (const auto& assignment : result.mAssignments)
        {
            const auto actor = std::find_if(snapshot.mActors.begin(), snapshot.mActors.end(),
                [&](const ActorSnapshot& value) { return value.mId == assignment.mActor; });
            Require(actor != snapshot.mActors.end(), "each assignment must name an actor");
            Require(std::find(actor->mCandidates.begin(), actor->mCandidates.end(),
                assignment.mDestination) != actor->mCandidates.end(),
                "each assignment must use a legal actor candidate");
            Require(std::find(used.begin(), used.end(), assignment.mDestination) == used.end(),
                "a projected cell and layer may be used only once");
            used.push_back(assignment.mDestination);
        }
    }

    void TestRowsAndTailCentering()
    {
        const auto odd = BuildLayout({10, 10}, {10, 13.5}, 7);
        Require(odd.mValid && odd.mColumns == 3 && odd.mRows == 3 && odd.mSlots.size() == 7 &&
            Near(odd.mPitch, 1.1), "default drag length must yield three columns and three rows");
        Require(Near(odd.mDepthAxis.mX, 0) && Near(odd.mDepthAxis.mY, 1) &&
            Near(odd.mWidthAxis.mX, -1) && Near(odd.mWidthAxis.mY, 0),
            "drag direction must be depth and the perpendicular must be width");
        Require(Near(odd.mLeft.mX, 11.75) && Near(odd.mRight.mX, 8.25) &&
            Near(odd.mLeft.mY, 10) && Near(odd.mRight.mY, 10),
            "width endpoints must span the requested drag length through the anchor");
        Require(Near(odd.mSlots[1].mX, 10) && Near(odd.mSlots[1].mY, 8.9) &&
            Near(odd.mSlots[4].mX, 10) && Near(odd.mSlots[4].mY, 10) &&
            Near(odd.mSlots[6].mX, 10) && Near(odd.mSlots[6].mY, 11.1),
            "each row center, including a one-car tail, must lie on the centered depth axis");

        const auto even = BuildLayout({10, 10}, {10, 13.5}, 6);
        Require(even.mColumns == 3 && even.mRows == 2 &&
            Near(even.mSlots[1].mY, 9.45) && Near(even.mSlots[4].mY, 10.55),
            "an even number of rows must straddle the anchor at half-pitch offsets");
        const auto twoTail = BuildLayout({10, 10}, {10, 13.5}, 5);
        Require(twoTail.mRows == 2 && Near(twoTail.mSlots[3].mX, 10.55) &&
            Near(twoTail.mSlots[4].mX, 9.45),
            "a two-car tail must center independently of a three-car full row");
    }

    void TestShortWidthAndRotation()
    {
        const auto narrow = BuildLayout({10, 10}, {10.3, 10}, 4);
        Require(narrow.mValid && narrow.mColumns == 1 && narrow.mRows == 4 &&
            Near(narrow.mSlots[0].mX, 8.35) && Near(narrow.mSlots[3].mX, 11.65),
            "a valid short drag must create one centered column");
        const auto rotated = BuildLayout({5, 5}, {8.5, 5}, 3);
        Require(rotated.mValid && Near(rotated.mDepthAxis.mX, 1) &&
            Near(rotated.mDepthAxis.mY, 0) && Near(rotated.mWidthAxis.mX, 0) &&
            Near(rotated.mWidthAxis.mY, 1) && Near(rotated.mSlots[1].mY, 5),
            "a horizontal drag must rotate the width axis and row centers");
    }

    void TestInvalidGeometryAndCellBudget()
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        Require(!BuildLayout({0, 0}, {0.2, 0}, 1).mValid &&
            !BuildLayout({0, 0}, {1, 0}, 0).mValid &&
            !BuildLayout({0, 0}, {1, 0}, 129).mValid &&
            !BuildLayout({nan, 0}, {1, 0}, 1).mValid &&
            !BuildLayout({0, 0}, {inf, 0}, 1).mValid &&
            !BuildLayout({0, 0}, {1, 0}, 1, 0, 0.3).mValid &&
            !BuildLayout({0, 0}, {1, 0}, 1, 0.4, -0.1).mValid &&
            !BuildLayout({0, 0}, {1, 0}, 1, 0.4, nan).mValid,
            "invalid drag, count and dimensions must be rejected before integer conversion");

        const auto layout = BuildLayout({0.2, 0.2}, {0.2, 3.7}, 7);
        const auto cells = BuildPlacementCells(layout);
        Require(!cells.empty() && cells.size() <= MAX_PLACEMENT_CELLS,
            "near-edge slots must return a bounded set of on-map cells");
        for (const auto& cell : cells)
            Require(cell.mX >= 0 && cell.mY >= 0 && cell.mX < 512 && cell.mY < 512,
                "placement cells must stay on the game grid");
        auto deduplicated = cells;
        deduplicated.erase(std::unique(deduplicated.begin(), deduplicated.end()), deduplicated.end());
        Require(deduplicated == cells, "overlapping slot neighborhoods must be deduplicated");

        const auto wide = BuildLayout({100, 100}, {100, 142}, 128, 0.4, 2.6);
        const auto tooManyCells = BuildPlacementCells(wide);
        Require(wide.mValid && tooManyCells.empty(),
            "more than 1024 distinct placement cells must reject the whole layout");
        auto snapshot = UniformSnapshot(wide, {{100, 100, false}});
        const auto rejected = PlanPlacement(wide, snapshot);
        Require(rejected.mBudgetExceeded && rejected.mAssignments.empty() &&
            rejected.mUnassignedActors.size() == 128,
            "placement-cell budget failure must reject every actor without truncation");
    }

    void TestSharedLegalityAndBridgeLayers()
    {
        const auto layout = BuildLayout({10, 10}, {10, 12}, 2);
        const std::vector<Cell> shared {{9, 10, false}, {11, 10, false}};
        Snapshot snapshot {{}, {
            Actor(2, {11, 10, false}, {{30, 30, false}, shared[1], shared[0], shared[0]}),
            Actor(1, {9, 10, false}, {{8, 10, false}, shared[0], shared[1]}) }};
        const auto result = PlanPlacement(layout, snapshot);
        RequireCompleteAndUnique(snapshot, result);
        Require(result.mAssignments == std::vector<Assignment> {{1, shared[0]}, {2, shared[1]}},
            "only common legal cells may be selected, then near actors should keep their cells");

        for (auto& actor : snapshot.mActors)
        {
            std::reverse(actor.mCandidates.begin(), actor.mCandidates.end());
        }
        std::reverse(snapshot.mActors.begin(), snapshot.mActors.end());
        Require(PlanPlacement(layout, snapshot).mAssignments == result.mAssignments,
            "actor and candidate input permutations must not change the placement");

        Snapshot bridge {{}, {
            Actor(1, {9, 10, true}, {{9, 10, true}, {11, 10, true}}),
            Actor(2, {11, 10, true}, {{9, 10, true}, {11, 10, true}}) }};
        RequireCompleteAndUnique(bridge, PlanPlacement(layout, bridge));
        for (const auto& assignment : PlanPlacement(layout, bridge).mAssignments)
            Require(assignment.mDestination.mOnBridge, "uniform bridge teams must preserve bridge-layer cells");
        bridge.mActors[1].mCandidates = shared;
        const auto mixed = PlanPlacement(layout, bridge);
        Require(mixed.mAssignments.empty() && mixed.mUnassignedActors == std::vector<ActorId> {1, 2},
            "mixed ground and bridge teams have no all-team common destination layer");
    }

    void TestIncompleteAndIdentityFailures()
    {
        const auto layout = BuildLayout({10, 10}, {10, 12}, 2);
        Snapshot oneCell {{}, {Actor(1, {10, 10, false}, {{10, 10, false}}),
            Actor(2, {11, 10, false}, {{10, 10, false}})}};
        const auto rejected = PlanPlacement(layout, oneCell);
        Require(rejected.mAssignments.empty() && rejected.mUnassignedActors == std::vector<ActorId> {1, 2},
            "one legal cell for two actors must reject the entire team");
        const auto narrow = BuildLayout({10, 10}, {10.3, 10}, 3);
        const std::vector<Cell> hallCells {{9, 10, false}, {12, 10, false}, {12, 11, false}};
        // 第一、二槽只能投向同一格；即使共有三格，也不存在三槽一一投影。
        const auto hall = PlanPlacement(narrow, UniformSnapshot(narrow, hallCells));
        Require(hall.mAssignments.empty() && hall.mUnassignedActors.size() == 3 &&
            !hall.mBudgetExceeded, "a Hall-conflict must reject every actor without a partial placement");
        oneCell.mActors[0].mCandidates = {{100, 100, false}, {101, 100, false}};
        oneCell.mActors[1].mCandidates = oneCell.mActors[0].mCandidates;
        Require(PlanPlacement(layout, oneCell).mAssignments.empty(),
            "a distant legal cell cannot substitute for a slot more than 1.5 cells away");
        oneCell.mActors[1].mId = 1;
        Require(PlanPlacement(layout, oneCell).mAssignments.empty(),
            "duplicate actor identities must reject the entire plan");
        oneCell.mActors[1].mId = 0;
        Require(PlanPlacement(layout, oneCell).mAssignments.empty(),
            "zero actor identity must reject the entire plan");
        oneCell.mActors.pop_back();
        Require(PlanPlacement(layout, oneCell).mAssignments.empty() &&
            PlanPlacement(layout, oneCell).mUnassignedActors == std::vector<ActorId>{1},
            "actor-to-slot count mismatch must reject all supplied actors");
    }

    void TestStationaryAndMaximumTeam()
    {
        const auto single = BuildLayout({10, 10}, {10, 10.5}, 1);
        const Snapshot stopped {{}, {Actor(9, {10, 10, false}, {{10, 10, false}})}};
        Require(PlanPlacement(single, stopped).mAssignments ==
            std::vector<Assignment> {{9, {10, 10, false}}},
            "a stopped actor already occupying its only legal slot must be accepted");

        const auto large = BuildLayout({100, 100}, {100, 115}, 128);
        const auto cells = BuildPlacementCells(large);
        Require(large.mValid && !cells.empty() && cells.size() <= MAX_PLACEMENT_CELLS,
            "the largest team must have a bounded candidate set");
        const auto snapshot = UniformSnapshot(large, cells);
        RequireCompleteAndUnique(snapshot, PlanPlacement(large, snapshot));

        const auto oversized = BuildLayout({100, 100}, {100, 115}, 129);
        auto tooMany = UniformSnapshot(large, cells);
        tooMany.mActors.push_back(Actor(129, {100, 100, false}, cells));
        const auto result = PlanPlacement(oversized, tooMany);
        Require(result.mBudgetExceeded && result.mAssignments.empty() &&
            result.mUnassignedActors.size() == 129,
            "more than 128 actors must fail as one unit");
    }

    void TestMixedPhysicalDemandAndLogicalPreview()
    {
        const std::vector<ActorProfile> profiles {
            {1, ActorKind::Vehicle, OccupancyPlane::Ground, false},
            {2, ActorKind::Infantry, OccupancyPlane::Ground, false},
            {3, ActorKind::Infantry, OccupancyPlane::Ground, false},
            {4, ActorKind::Infantry, OccupancyPlane::Ground, false}};
        const auto layout = BuildLayout({10, 10}, {10, 12}, profiles);
        Require(layout.mValid && layout.mProfiles == profiles && layout.mSlots.size() == 4 &&
            layout.mPlanes.size() == 1 && layout.mPlanes[0].mCellCenters.size() == 2 &&
            layout.mInfantryCapacity == 3,
            "three infantry must share one physical drag slot while preview keeps four actor dots");
        const Cell a {9, 10, false}, b {11, 10, false};
        Snapshot snapshot {{}, {
            Actor(1, a, {a, b}), Actor(2, a, {a}),
            Actor(3, a, {a}), Actor(4, a, {a})}};
        for (std::size_t index = 1; index < snapshot.mActors.size(); ++index)
            snapshot.mActors[index].mKind = ActorKind::Infantry;
        const auto result = PlanPlacement(layout, snapshot);
        Require(!result.mBudgetExceeded && result.mAssignments.size() == 4 &&
            result.mUnassignedActors.empty() &&
            ra_commands::auto_formation::HasValidReservations(result.mAssignments),
            "drag placement must reserve one vehicle cell plus one three-seat infantry cell");
        Require(result.mAssignments[0].mDestination == b &&
            std::all_of(result.mAssignments.begin() + 1, result.mAssignments.end(),
                [&](const Assignment& item) { return item.mDestination == a; }),
            "the vehicle may not consume the infantry group's only legal physical cell");
        snapshot.mActors[2].mPlane = OccupancyPlane::Air;
        const auto changed = PlanPlacement(layout, snapshot);
        Require(changed.mAssignments.empty() && changed.mUnassignedActors.size() == 4,
            "a plane change after gesture capture must reject the frozen formation");
    }

    void TestMixedCapacityFallbackAndPlanes()
    {
        const std::vector<ActorProfile> profiles {
            {1, ActorKind::Vehicle, OccupancyPlane::Ground, false},
            {2, ActorKind::Infantry, OccupancyPlane::Ground, false},
            {3, ActorKind::Infantry, OccupancyPlane::Ground, false},
            {4, ActorKind::Infantry, OccupancyPlane::Ground, false}};
        const auto layout = BuildLayout({10, 10}, {10, 12}, profiles, 0.4, 0.3, 2);
        Require(layout.mValid && layout.mInfantryCapacity == 2 &&
            layout.mPlanes[0].mCellCenters.size() == 3 && layout.mSlots.size() == 4,
            "external occupancy reducing infantry capacity must increase physical drag demand");
        const Cell a {9, 10, false}, b {11, 10, false}, c {10, 9, false};
        Snapshot snapshot {{}, {Actor(1, c, {c}), Actor(2, a, {a, b}),
            Actor(3, a, {a, b}), Actor(4, a, {a, b})}};
        for (std::size_t index = 1; index < snapshot.mActors.size(); ++index)
        {
            snapshot.mActors[index].mKind = ActorKind::Infantry;
            snapshot.mActors[index].mCapacityLimits = {{a, 2}, {b, 1}};
        }
        const auto packed = PlanPlacement(layout, snapshot);
        Require(!packed.mBudgetExceeded && packed.mAssignments.size() == 4 &&
            packed.mUnassignedActors.empty() &&
            ra_commands::auto_formation::HasValidReservations(packed.mAssignments),
            "capacity-two drag layout must place all four members without partial issue");

        const std::vector<ActorProfile> layered {
            {1, ActorKind::Vehicle, OccupancyPlane::Ground, false},
            {2, ActorKind::Infantry, OccupancyPlane::Air, false},
            {3, ActorKind::Infantry, OccupancyPlane::Air, false},
            {4, ActorKind::Infantry, OccupancyPlane::Air, false}};
        const auto layeredLayout = BuildLayout({10, 10}, {10, 11}, layered);
        Require(layeredLayout.mValid && layeredLayout.mPlanes.size() == 2 &&
            layeredLayout.mPlanes[0].mCellCenters.size() == 1 &&
            layeredLayout.mPlanes[1].mCellCenters.size() == 1,
            "ground and air physical demands must be laid out independently");
        for (auto& actor : snapshot.mActors)
        {
            actor.mCandidates = {{10, 10, false}};
            actor.mCapacityLimits.clear();
            if (actor.mId != 1) actor.mPlane = OccupancyPlane::Air;
        }
        const auto both = PlanPlacement(layeredLayout, snapshot);
        Require(!both.mBudgetExceeded && both.mAssignments.size() == 4 &&
            ra_commands::auto_formation::HasValidReservations(both.mAssignments),
            "ground vehicle and air infantry may occupy the same XY under separate plane keys");
    }

    void TestMixed128ActorBound()
    {
        std::vector<ActorProfile> profiles;
        for (ActorId id = 1; id <= 128; ++id)
            profiles.push_back({id, id <= 64 ? ActorKind::Vehicle : ActorKind::Infantry,
                OccupancyPlane::Ground, false});
        const auto layout = BuildLayout({100, 100}, {100, 120}, profiles);
        const auto cells = BuildPlacementCells(layout);
        Require(layout.mValid && layout.mSlots.size() == 128 &&
            layout.mPlanes[0].mCellCenters.size() == 86 &&
            !cells.empty() && cells.size() <= MAX_PLACEMENT_CELLS,
            "mixed maximum team must use 64 + ceil(64/3) physical cells within candidate budget");
        Snapshot snapshot;
        for (const auto& profile : profiles)
        {
            auto actor = Actor(profile.mId, {100, 100, false}, cells);
            actor.mKind = profile.mKind;
            snapshot.mActors.push_back(std::move(actor));
        }
        const auto result = PlanPlacement(layout, snapshot);
        Require(!result.mBudgetExceeded && result.mAssignments.size() == 128 &&
            result.mUnassignedActors.empty() &&
            ra_commands::auto_formation::HasValidReservations(result.mAssignments),
            "the bounded physical flow must accept a complete 128-member mixed team");
    }
}

void RunDragFormationPlannerTests()
{
    TestRowsAndTailCentering();
    TestShortWidthAndRotation();
    TestInvalidGeometryAndCellBudget();
    TestSharedLegalityAndBridgeLayers();
    TestIncompleteAndIdentityFailures();
    TestStationaryAndMaximumTeam();
    TestMixedPhysicalDemandAndLogicalPreview();
    TestMixedCapacityFallbackAndPlanes();
    TestMixed128ActorBound();
}
