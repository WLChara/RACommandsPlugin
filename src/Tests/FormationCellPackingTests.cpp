#include "Commands/AutoFormationCommand/FormationCellPacking.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    using namespace ra_commands::auto_formation;

    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    ActorSnapshot Actor(ActorId id, ActorKind kind, OccupancyPlane plane,
        Cell current, std::vector<Cell> candidates,
        std::vector<CandidateCapacity> limits = {})
    {
        return {id, current, {static_cast<double>(current.mX), static_cast<double>(current.mY)},
            std::move(candidates), kind, plane, std::move(limits)};
    }

    PlaneLayout Ground(std::vector<Center> centers, bool bridge = false)
    {
        return {OccupancyPlane::Ground, bridge, std::move(centers)};
    }

    void RequireComplete(const Snapshot& snapshot, const PlanResult& result)
    {
        Require(!result.mBudgetExceeded && result.mAssignments.size() == snapshot.mActors.size() &&
            result.mUnassignedActors.empty() && HasValidReservations(result.mAssignments),
            "a full packing must include every actor and obey the logical cell capacity");
    }

    void TestReservationValidatorAndCounts()
    {
        const Cell ground {10, 10, false};
        const Cell bridge {10, 10, true};
        Require(HasValidReservations({{1, ground, ActorKind::Vehicle, OccupancyPlane::Ground, 0},
            {2, ground, ActorKind::Infantry, OccupancyPlane::Air, 0},
            {3, ground, ActorKind::Infantry, OccupancyPlane::Air, 1},
            {4, ground, ActorKind::Infantry, OccupancyPlane::Air, 2},
            {5, bridge, ActorKind::Vehicle, OccupancyPlane::Ground, 0}}),
            "land, air and bridge reservations use independent resource keys");
        Require(!HasValidReservations({{1, ground, ActorKind::Vehicle, OccupancyPlane::Ground, 0},
            {2, ground, ActorKind::Infantry, OccupancyPlane::Ground, 1}}) &&
            !HasValidReservations({{1, ground, ActorKind::Infantry, OccupancyPlane::Ground, 0},
                {2, ground, ActorKind::Infantry, OccupancyPlane::Ground, 0}}) &&
            !HasValidReservations({{1, ground, ActorKind::Infantry, OccupancyPlane::Ground, 3}}) &&
            !HasValidReservations({{1, ground, ActorKind::Vehicle, OccupancyPlane::Air, 0},
                {1, {11, 10, false}, ActorKind::Vehicle, OccupancyPlane::Air, 0}}),
            "mixed classes, duplicate seats, fourth seat and duplicate IDs must be rejected");
        const std::vector<ActorProfile> profiles {
            {1, ActorKind::Vehicle, OccupancyPlane::Ground, false},
            {2, ActorKind::Infantry, OccupancyPlane::Ground, false},
            {3, ActorKind::Infantry, OccupancyPlane::Ground, false},
            {4, ActorKind::Infantry, OccupancyPlane::Ground, false},
            {5, ActorKind::Infantry, OccupancyPlane::Ground, false},
            {6, ActorKind::Vehicle, OccupancyPlane::Air, false}};
        Require(RequiredCellCount(profiles, OccupancyPlane::Ground, false) == 3 &&
            RequiredCellCount(profiles, OccupancyPlane::Ground, false, 2) == 3 &&
            RequiredCellCount(profiles, OccupancyPlane::Ground, false, 1) == 5 &&
            RequiredCellCount(profiles, OccupancyPlane::Air, false) == 1,
            "physical demand is V + ceil(I/capacity) per resource plane");
    }

    void TestPhysicalFlowAllowsVehicleSlotSwap()
    {
        const Cell a {10, 10, false}, b {11, 10, false};
        Snapshot snapshot {{10.5, 10}, {
            Actor(1, ActorKind::Vehicle, OccupancyPlane::Ground, b, {b}),
            Actor(2, ActorKind::Vehicle, OccupancyPlane::Ground, a, {a})}};
        const auto result = PlanCellPacking(snapshot, {Ground({{10, 10}, {11, 10}})}, true, 1.5);
        RequireComplete(snapshot, result);
        Require(result.mAssignments[0].mDestination == b &&
            result.mAssignments[1].mDestination == a,
            "a vehicle must be able to use the other ideal slot through a shared physical cell");

        snapshot.mActors[0].mCandidates = {a, b};
        snapshot.mActors[1].mCandidates = {a, b};
        snapshot.mActors[0].mCurrentCell = a;
        snapshot.mActors[1].mCurrentCell = b;
        snapshot.mActors[0].mPosition = {10.9, 10};
        snapshot.mActors[1].mPosition = {10.1, 10};
        const auto retained = PlanCellPacking(snapshot, {Ground({{10, 10}, {11, 10}})}, true, 1.5);
        RequireComplete(snapshot, retained);
        Require(retained.mAssignments[0].mDestination == a &&
            retained.mAssignments[1].mDestination == b,
            "vehicles in chosen cells must remain there even when subcell positions favor a swap");
    }

    void TestInfantryPackingAndScarceCellPriority()
    {
        const Cell a {10, 10, false}, b {11, 10, false};
        Snapshot snapshot {{10.5, 10}, {
            Actor(1, ActorKind::Vehicle, OccupancyPlane::Ground, a, {a, b}),
            Actor(2, ActorKind::Infantry, OccupancyPlane::Ground, a, {a}),
            Actor(3, ActorKind::Infantry, OccupancyPlane::Ground, a, {a}),
            Actor(4, ActorKind::Infantry, OccupancyPlane::Ground, a, {a})}};
        const auto complete = PlanCellPacking(snapshot, {Ground({{10, 10}, {11, 10}})}, true, 1.5);
        RequireComplete(snapshot, complete);
        Require(complete.mAssignments[0].mDestination == b &&
            std::all_of(complete.mAssignments.begin() + 1, complete.mAssignments.end(),
                [&](const Assignment& item) { return item.mDestination == a; }),
            "vehicle and infantry requests must compete in one flow so infantry keeps its only cell");

        snapshot.mActors[0].mCandidates = {a};
        const auto partial = PlanCellPacking(snapshot, {Ground({{10, 10}, {11, 10}})}, false, 1.5);
        Require(!partial.mBudgetExceeded && partial.mAssignments.size() == 3 &&
            partial.mUnassignedActors == std::vector<ActorId>{1} &&
            HasValidReservations(partial.mAssignments),
            "one scarce cell must favor three actual infantry over one vehicle");
        const auto rejected = PlanCellPacking(snapshot, {Ground({{10, 10}, {11, 10}})}, true, 1.5);
        Require(rejected.mAssignments.empty() && rejected.mUnassignedActors.size() == 4,
            "strict placement must reject an incomplete mixed team");
    }

    void TestFourthInfantryUsesAnotherCell()
    {
        const Cell a {10, 10, false}, b {11, 10, false};
        Snapshot snapshot {{10.5, 10}, {}};
        for (ActorId id = 1; id <= 4; ++id)
            snapshot.mActors.push_back(Actor(id, ActorKind::Infantry,
                OccupancyPlane::Ground, a, {a, b}));
        const auto result = PlanCellPacking(snapshot, {Ground({{10, 10}, {11, 10}})}, true, 1.5);
        RequireComplete(snapshot, result);
        const auto atA = std::count_if(result.mAssignments.begin(), result.mAssignments.end(),
            [&](const Assignment& item) { return item.mDestination == a; });
        const auto atB = std::count_if(result.mAssignments.begin(), result.mAssignments.end(),
            [&](const Assignment& item) { return item.mDestination == b; });
        Require(atA + atB == 4 && atA <= 3 && atB <= 3 && atA > 0 && atB > 0,
            "four infantry must use two physical cells without exceeding three seats each");
    }

    void TestAirGroundAndBridgeIndependent()
    {
        const Cell a {10, 10, false};
        Snapshot snapshot {{10, 10}, {
            Actor(1, ActorKind::Vehicle, OccupancyPlane::Ground, a, {a}),
            Actor(2, ActorKind::Infantry, OccupancyPlane::Air, a, {a}),
            Actor(3, ActorKind::Infantry, OccupancyPlane::Air, a, {a}),
            Actor(4, ActorKind::Infantry, OccupancyPlane::Air, a, {a})}};
        const auto result = PlanCellPacking(snapshot,
            {Ground({{10, 10}}), {OccupancyPlane::Air, false, {{10, 10}}}}, true, 1.5);
        RequireComplete(snapshot, result);
        Require(std::all_of(result.mAssignments.begin(), result.mAssignments.end(),
            [&](const Assignment& item) { return item.mDestination == a; }),
            "ground vehicle and air infantry may use the same XY independently");

        const Cell bridge {10, 10, true};
        Snapshot twoLevels {{10, 10}, {
            Actor(1, ActorKind::Vehicle, OccupancyPlane::Ground, a, {a}),
            Actor(2, ActorKind::Vehicle, OccupancyPlane::Ground, bridge, {bridge})}};
        RequireComplete(twoLevels, PlanCellPacking(twoLevels,
            {Ground({{10, 10}}), Ground({{10, 10}}, true)}, true, 1.5));
    }

    void TestExternalInfantryQuotaAndCompactFallback()
    {
        const Cell a {10, 10, false}, b {11, 10, false};
        Snapshot snapshot {{10.5, 10}, {}};
        for (ActorId id = 1; id <= 3; ++id)
            snapshot.mActors.push_back(Actor(id, ActorKind::Infantry,
                OccupancyPlane::Ground, a, {a, b}, {{a, 2}, {b, 1}}));
        const auto fullThree = PlanCellPacking(snapshot, {Ground({{10, 10}})}, true, 1.5, 3);
        Require(fullThree.mAssignments.empty(),
            "a three-person bundle cannot use a cell with only two free places");
        RequireComplete(snapshot, PlanCellPacking(snapshot,
            {Ground({{10, 10}, {11, 10}})}, true, 1.5, 2));
        const auto compact = PlanCompactPacking(snapshot);
        RequireComplete(snapshot, compact);
        Require(std::count_if(compact.mAssignments.begin(), compact.mAssignments.end(),
            [&](const Assignment& item) { return item.mDestination == a; }) == 2,
            "compact planner must expand from capacity three to two when native quota requires it");
    }

    void TestDeterminismAndBudget()
    {
        const Cell a {10, 10, false}, b {11, 10, false};
        Snapshot snapshot {{10.5, 10}, {
            Actor(3, ActorKind::Infantry, OccupancyPlane::Ground, a, {a, b}),
            Actor(1, ActorKind::Vehicle, OccupancyPlane::Ground, b, {a, b}),
            Actor(2, ActorKind::Infantry, OccupancyPlane::Ground, a, {a, b})}};
        const auto layout = std::vector<PlaneLayout>{Ground({{10, 10}, {11, 10}})};
        const auto first = PlanCellPacking(snapshot, layout, true, 1.5);
        RequireComplete(snapshot, first);
        std::reverse(snapshot.mActors.begin(), snapshot.mActors.end());
        for (auto& actor : snapshot.mActors) std::reverse(actor.mCandidates.begin(), actor.mCandidates.end());
        const auto second = PlanCellPacking(snapshot, layout, true, 1.5);
        Require(first.mAssignments == second.mAssignments,
            "actor and candidate input order must not change reservations or logical seats");
        snapshot.mActors.front().mPosition.mX = std::numeric_limits<double>::infinity();
        Require(PlanCellPacking(snapshot, layout, true, 1.5).mAssignments.empty(),
            "non-finite actor positions must be rejected before cost conversion");
        snapshot.mActors.front().mPosition.mX = 1000000000000.0;
        Require(PlanCellPacking(snapshot, layout, true, 1.5).mAssignments.empty(),
            "unrepresentable actor positions must not enter matching costs");
        snapshot.mActors.front().mPosition.mX = 10.0;
        snapshot.mActors.front().mId = snapshot.mActors.back().mId;
        Require(PlanCellPacking(snapshot, layout, true, 1.5).mAssignments.empty(),
            "duplicate actor identities must reject the complete plan");
        Snapshot large {{100, 100}, {}};
        for (ActorId id = 1; id <= 257; ++id)
            large.mActors.push_back(Actor(id, ActorKind::Infantry,
                OccupancyPlane::Ground, {100, 100, false}, {a}));
        const auto over = PlanCellPacking(large, {Ground({{100, 100}})}, true, 1.5);
        Require(over.mBudgetExceeded && over.mAssignments.empty() &&
            over.mUnassignedActors.size() == 257,
            "more than 256 actors must reject the whole plan");
    }
}

void RunFormationCellPackingTests()
{
    TestReservationValidatorAndCounts();
    TestPhysicalFlowAllowsVehicleSlotSwap();
    TestInfantryPackingAndScarceCellPriority();
    TestFourthInfantryUsesAnotherCell();
    TestAirGroundAndBridgeIndependent();
    TestExternalInfantryQuotaAndCompactFallback();
    TestDeterminismAndBudget();
}
