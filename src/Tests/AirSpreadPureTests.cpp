#include "Commands/AirSpreadCommand/AirSpreadPlanner.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    using namespace ra_commands::air_spread;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    ActorCandidates Actor(ActorId id, ActorKind kind, std::vector<Cell> cells)
    {
        return {id, kind, std::move(cells)};
    }

    void TestInfantryCapacityThreeAndStableOrder()
    {
        const Cell near{4, 8};
        const Cell next{5, 8};
        const std::vector<ActorCandidates> actors = {
            Actor(10, ActorKind::FlyingInfantry, {near, next}),
            Actor(11, ActorKind::FlyingInfantry, {near, next}),
            Actor(12, ActorKind::FlyingInfantry, {near, next}),
            Actor(13, ActorKind::FlyingInfantry, {near, next}),
        };

        const auto result = Plan(actors, 3);
        Require(result.Assignments == std::vector<Assignment>{
            {10, ActorKind::FlyingInfantry, near},
            {11, ActorKind::FlyingInfantry, near},
            {12, ActorKind::FlyingInfantry, near},
            {13, ActorKind::FlyingInfantry, next}},
            "infantry capacity three should fill each ordered cell deterministically");
        Require(result.UnassignedActors.empty(), "all four infantry should fit across two cells");
    }

    void TestFlyingUnitCapacityOne()
    {
        const Cell first{1, 2};
        const Cell second{2, 2};
        const auto result = Plan({
            Actor(20, ActorKind::FlyingUnit, {first, second}),
            Actor(21, ActorKind::FlyingUnit, {first, second}),
        }, 1);

        Require(result.Assignments == std::vector<Assignment>{
            {20, ActorKind::FlyingUnit, first},
            {21, ActorKind::FlyingUnit, second}},
            "flying units should use at most one actor per cell");
    }

    void TestAircraftCapacityOne()
    {
        const Cell landing{7, 3};
        const auto result = Plan({
            Actor(30, ActorKind::Aircraft, {landing}),
            Actor(31, ActorKind::Aircraft, {landing}),
        }, 1);

        Require(result.Assignments.size() == 1 &&
            result.Assignments[0] == Assignment{30, ActorKind::Aircraft, landing},
            "aircraft group should share the single-actor cell capacity");
        Require(result.UnassignedActors == std::vector<ActorId>{31},
            "aircraft without an available candidate cell should be unassigned");
    }

    void TestCandidateShortageZeroCapacityAndInitialOccupancy()
    {
        const Cell only{0, 0};
        const std::vector<ActorCandidates> actors = {
            Actor(1, ActorKind::FlyingInfantry, {only}),
            Actor(2, ActorKind::FlyingUnit, {}),
            Actor(3, ActorKind::Aircraft, {only}),
        };

        const auto limited = Plan(actors, 1);
        Require(limited.Assignments.size() == 1 && limited.Assignments[0].Actor == 1,
            "planner must never invent a cell when valid candidates run out");
        Require(limited.UnassignedActors == std::vector<ActorId>{2, 3},
            "actors with no available candidates should retain input order as unassigned");

        const auto zeroCapacity = Plan(actors, 0);
        Require(zeroCapacity.Assignments.empty() &&
            zeroCapacity.UnassignedActors == std::vector<ActorId>{1, 2, 3},
            "zero capacity should leave every unique actor unassigned");

        const auto occupied = Plan({
            Actor(4, ActorKind::FlyingInfantry, {only, Cell{1, 0}}),
            Actor(5, ActorKind::FlyingInfantry, {only, Cell{1, 0}}),
        }, 3, {{only, 3}});
        Require(occupied.Assignments == std::vector<Assignment>{
            {4, ActorKind::FlyingInfantry, {1, 0}},
            {5, ActorKind::FlyingInfantry, {1, 0}}},
            "a cell at initial capacity must be skipped in favor of the next candidate");
    }

    void TestGroupsCanOverlapAndDuplicateIdsAreSuppressed()
    {
        const Cell shared{9, 9};
        const auto infantry = Plan({
            Actor(40, ActorKind::FlyingInfantry, {shared}),
            Actor(40, ActorKind::FlyingInfantry, {shared}),
        }, 3);
        const auto aircraft = Plan({
            Actor(50, ActorKind::Aircraft, {shared}),
        }, 1);

        Require(infantry.Assignments.size() == 1 && infantry.UnassignedActors.empty(),
            "duplicate actor IDs must not create duplicate output records");
        Require(aircraft.Assignments.size() == 1 &&
            infantry.Assignments[0].Destination == aircraft.Assignments[0].Destination,
            "independent group plans may assign different actors to the same cell");
    }
}

void RunAirSpreadTests()
{
    TestInfantryCapacityThreeAndStableOrder();
    TestFlyingUnitCapacityOne();
    TestAircraftCapacityOne();
    TestCandidateShortageZeroCapacityAndInitialOccupancy();
    TestGroupsCanOverlapAndDuplicateIdsAreSuppressed();
}
