#include "Commands/AutoCrush/AutoCrushPlanner.h"

#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    using namespace ra_commands::auto_crush;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void RequireNear(double actual, double expected, const char* message)
    {
        Require(std::abs(actual - expected) < 0.000001, message);
    }

    std::vector<Cell> EastCells(Cell origin, int count)
    {
        std::vector<Cell> cells;
        for (int step = 1; step <= count; ++step)
        {
            cells.push_back({origin.mX + step, origin.mY});
        }
        return cells;
    }

    TargetPrediction Target(TargetId id, Cell current, Cell next,
        std::uint32_t transitionSteps = 0, double confidence = 1.0)
    {
        return {id, current, next, transitionSteps, confidence};
    }

    CrusherSnapshot Crusher(CrusherId id, Cell current, Facing facing,
        std::vector<Cell> traversable, std::vector<TargetPrediction> targets,
        std::vector<Cell> nonStoppable = {})
    {
        return {id, current, facing, std::move(traversable),
            std::move(nonStoppable), std::move(targets)};
    }

    void TestCrushesSeveralInfantryBeforeStopping()
    {
        const auto result = Plan({{Crusher(10, {0, 0}, Facing::East,
            EastCells({0, 0}, 8), {
                Target(30, {1, 0}, {1, 0}),
                Target(20, {2, 0}, {2, 0}),
                Target(20, {2, 0}, {2, 0})})}});

        Require(result.mMoves.size() == 1, "a profitable straight route should be planned");
        const auto& move = result.mMoves[0];
        Require(move.mDestination == Cell{3, 0} &&
            move.mPath == std::vector<Cell>{{1, 0}, {2, 0}, {3, 0}},
            "the destination must lie beyond both infantry and the path must be straight");
        Require(move.mExpectedTargetIds == std::vector<TargetId>{20, 30},
            "target IDs must be stable and deduplicated");
        RequireNear(move.mExpectedCrushCount, 2.0, "each infantry should count once");
        RequireNear(move.mScore, 2.0 / 3.0, "score should use expected crushes per travel step");
    }

    void TestInitialFacingChangesPreference()
    {
        const auto result = Plan({{Crusher(1, {0, 0}, Facing::East,
            {{0, -1}, {0, -2}, {1, 0}, {2, 0}},
            {Target(1, {0, -1}, {0, -1}), Target(2, {1, 0}, {1, 0})})}});

        Require(result.mMoves.size() == 1 &&
            result.mMoves[0].mDestination == Cell{2, 0} &&
            result.mMoves[0].mExpectedTargetIds == std::vector<TargetId>{2},
            "equal-distance infantry straight ahead should beat a turning route");
        RequireNear(result.mMoves[0].mScore, 0.5, "straight route should have no turning cost");
    }

    void TestAdapterMarksMixedCells()
    {
        const auto blocked = Plan({{Crusher(1, {0, 0}, Facing::East,
            {{1, 0}, {3, 0}}, {Target(7, {1, 0}, {1, 0})})}});
        Require(blocked.mMoves.empty(),
            "an impassable cell behind the target must prevent a through-route");

        const auto marked = Plan({{Crusher(1, {0, 0}, Facing::East,
            {{1, 0}, {2, 0}, {3, 0}},
            {Target(7, {1, 0}, {1, 0})}, {{2, 0}})}});
        Require(marked.mMoves.size() == 1 &&
            marked.mMoves[0].mDestination == Cell{3, 0} &&
            marked.mMoves[0].mPath == std::vector<Cell>{{1, 0}, {2, 0}, {3, 0}} &&
            marked.mMoves[0].mExpectedTargetIds == std::vector<TargetId>{7},
            "adapter-excluded infantry must not score, and non-stoppable cells must not be endpoints");
    }

    void TestUsesOnlySuppliedMovementPrediction()
    {
        const auto moved = Plan({{Crusher(1, {0, 0}, Facing::East,
            EastCells({0, 0}, 3), {Target(5, {1, 0}, {2, 0}, 1, 1.0)})}});
        Require(moved.mMoves.size() == 1 &&
            moved.mMoves[0].mDestination == Cell{3, 0} &&
            moved.mMoves[0].mExpectedTargetIds == std::vector<TargetId>{5},
            "a predicted move from A to B should be intercepted at B with a cell behind it");

        const auto departed = Plan({{Crusher(1, {0, 0}, Facing::East,
            EastCells({0, 0}, 3), {Target(5, {1, 0}, {1, 1}, 1, 1.0)})}});
        Require(departed.mMoves.empty(),
            "a target predicted to leave the whole route must not cause a command");

        const auto uncertain = Plan({{Crusher(1, {0, 0}, Facing::East,
            EastCells({0, 0}, 3), {Target(5, {1, 0}, {2, 0}, 1, 0.25)})}});
        Require(uncertain.mMoves.size() == 1 &&
            uncertain.mMoves[0].mDestination == Cell{2, 0},
            "uncertain movement should favor the closer current-cell route");
        RequireNear(uncertain.mMoves[0].mExpectedCrushCount, 0.75,
            "uncertain prediction should conservatively lower expected yield");
    }

    void TestSeveralVehiclesRerouteAroundReservations()
    {
        const auto result = Plan({{
            Crusher(2, {0, 2}, Facing::NorthEast,
                {{1, 1}, {2, 0}, {1, 2}, {2, 2}},
                {Target(20, {1, 1}, {1, 1}), Target(21, {1, 2}, {1, 2})}),
            Crusher(1, {0, 0}, Facing::East,
                {{1, 0}, {2, 0}}, {Target(10, {1, 0}, {1, 0})})}});

        Require(result.mMoves.size() == 2, "both vehicles should get a profitable route");
        Require(result.mMoves[0].mCrusherId == 1 &&
            result.mMoves[0].mDestination == Cell{2, 0},
            "the highest-scored first route should reserve the shared destination");
        Require(result.mMoves[1].mCrusherId == 2 &&
            result.mMoves[1].mDestination == Cell{2, 2} &&
            result.mMoves[1].mExpectedTargetIds == std::vector<TargetId>{21},
            "a conflicting vehicle should use its next profitable route");
    }

    void TestSharedTargetsOnlyRewardOneVehicle()
    {
        const auto result = Plan({{
            Crusher(2, {0, -1}, Facing::South,
                {{0, 0}, {0, 1}}, {Target(8, {0, 0}, {0, 0})}),
            Crusher(1, {-1, 0}, Facing::East,
                {{0, 0}, {1, 0}}, {Target(8, {0, 0}, {0, 0})})}});

        Require(result.mMoves.size() == 1 && result.mMoves[0].mCrusherId == 1,
            "an already-assigned target must not reward another vehicle on an equal score");
    }

    void TestNearTimeCorridorConflict()
    {
        const auto result = Plan({{
            Crusher(2, {1, -2}, Facing::South,
                {{1, -1}, {1, 0}, {2, -2}, {3, -2}},
                {Target(20, {1, -1}, {1, -1}), Target(21, {2, -2}, {2, -2})}),
            Crusher(1, {0, 0}, Facing::East,
                {{1, 0}, {2, 0}}, {Target(10, {1, 0}, {1, 0})})}});

        Require(result.mMoves.size() == 2 &&
            result.mMoves[0].mDestination == Cell{2, 0} &&
            result.mMoves[1].mDestination == Cell{3, -2} &&
            result.mMoves[1].mExpectedTargetIds == std::vector<TargetId>{21},
            "routes reaching the same corridor cell one step apart must divert");
    }

    void TestNoTargetAndRadiusLimit()
    {
        Require(Plan({{Crusher(1, {0, 0}, Facing::East,
            EastCells({0, 0}, 8), {})}}).mMoves.empty(),
            "no target should yield no Move command");
        const auto edge = Plan({{Crusher(1, {0, 0}, Facing::East,
            EastCells({0, 0}, 9), {Target(8, {8, 0}, {8, 0})})}});
        Require(edge.mMoves.size() == 1 && edge.mMoves[0].mDestination == Cell{9, 0},
            "a target on the eighth cell needs a legal ninth-cell stopping point");
        Require(Plan({{Crusher(1, {0, 0}, Facing::East,
            EastCells({0, 0}, 8), {Target(8, {8, 0}, {8, 0})})}}).mMoves.empty(),
            "the eighth-cell target is skipped if no ninth stopping cell exists");
    }

    void TestStableTieOrder()
    {
        const auto tiedDirections = Plan({{Crusher(3, {0, 0}, Facing::NorthEast,
            {{1, 0}, {2, 0}, {0, -1}, {0, -2}},
            {Target(2, {1, 0}, {1, 0}), Target(1, {0, -1}, {0, -1})})}});
        Require(tiedDirections.mMoves.size() == 1 &&
            tiedDirections.mMoves[0].mDestination == Cell{0, -2},
            "equal-scored directions should use the stable clockwise enum order");

        const auto first = Plan({{
            Crusher(9, {20, 0}, Facing::East,
                {{21, 0}, {22, 0}}, {Target(9, {21, 0}, {21, 0})}),
            Crusher(4, {0, 0}, Facing::East,
                {{1, 0}, {2, 0}}, {Target(4, {1, 0}, {1, 0})})}});
        const auto second = Plan({{
            Crusher(4, {0, 0}, Facing::East,
                {{2, 0}, {1, 0}}, {Target(4, {1, 0}, {1, 0})}),
            Crusher(9, {20, 0}, Facing::East,
                {{22, 0}, {21, 0}}, {Target(9, {21, 0}, {21, 0})})}});
        Require(first.mMoves.size() == 2 && second.mMoves.size() == 2 &&
            first.mMoves[0].mCrusherId == 4 && first.mMoves[1].mCrusherId == 9 &&
            second.mMoves[0].mCrusherId == 4 && second.mMoves[1].mCrusherId == 9 &&
            first.mMoves[0].mDestination == second.mMoves[0].mDestination &&
            first.mMoves[1].mDestination == second.mMoves[1].mDestination,
            "input order must not change stable vehicle ordering or destinations");
    }
}

void RunAutoCrushPlannerTests()
{
    TestCrushesSeveralInfantryBeforeStopping();
    TestInitialFacingChangesPreference();
    TestAdapterMarksMixedCells();
    TestUsesOnlySuppliedMovementPrediction();
    TestSeveralVehiclesRerouteAroundReservations();
    TestSharedTargetsOnlyRewardOneVehicle();
    TestNearTimeCorridorConflict();
    TestNoTargetAndRadiusLimit();
    TestStableTieOrder();
}
