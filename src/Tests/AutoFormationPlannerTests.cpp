#include "Commands/AutoFormationCommand/AutoFormationPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
    using namespace ra_commands::auto_formation;

    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    ActorSnapshot Actor(ActorId id, Center position, std::vector<Cell> candidates)
    {
        return { id, {}, position, std::move(candidates) };
    }

    std::vector<Cell> Square(int low, int high)
    {
        std::vector<Cell> cells;
        for (int x = low; x <= high; ++x)
            for (int y = low; y <= high; ++y) cells.push_back({ x, y, false });
        return cells;
    }

    Snapshot Uniform(std::size_t count, const std::vector<Cell>& cells, Center center = {})
    {
        Snapshot snapshot { center, {} };
        for (std::size_t actor = 0; actor < count; ++actor)
            snapshot.mActors.push_back(Actor(actor + 1, { 20, 20 }, cells));
        return snapshot;
    }

    std::pair<int, int> Dimensions(const PlanResult& result)
    {
        int minX = std::numeric_limits<int>::max();
        int minY = minX;
        int maxX = std::numeric_limits<int>::min();
        int maxY = maxX;
        for (const auto& assignment : result.mAssignments)
        {
            minX = std::min(minX, assignment.mDestination.mX);
            minY = std::min(minY, assignment.mDestination.mY);
            maxX = std::max(maxX, assignment.mDestination.mX);
            maxY = std::max(maxY, assignment.mDestination.mY);
        }
        return { maxX - minX + 1, maxY - minY + 1 };
    }

    void RequireValid(const Snapshot& snapshot, const PlanResult& result)
    {
        std::vector<Cell> used;
        for (const auto& assignment : result.mAssignments)
        {
            const auto actor = std::find_if(snapshot.mActors.begin(), snapshot.mActors.end(),
                [&](const ActorSnapshot& item) { return item.mId == assignment.mActor; });
            Require(actor != snapshot.mActors.end(), "every assignment must identify a supplied actor");
            Require(std::find(actor->mCandidates.begin(), actor->mCandidates.end(), assignment.mDestination) != actor->mCandidates.end(),
                "every destination must be an actor-specific candidate");
            Require(std::find(used.begin(), used.end(), assignment.mDestination) == used.end(),
                "each cell and bridge layer must hold at most one actor");
            used.push_back(assignment.mDestination);
        }
    }

    void TestCenterPrecisionAndFirstIds()
    {
        const auto center = CalculateCenter({
            Actor(5, { 0.25, -1.5 }, {}), Actor(2, { 1.75, 2.5 }, {}),
            Actor(0, { 100, 100 }, {}), Actor(5, { 100, 100 }, {}) });
        Require(center.mX == 1.0 && center.mY == 0.5,
            "the center must retain subcell precision and count each nonzero ID once");
        Require(CalculateCenter({}).mX == 0.0, "empty selection must have zero center");
        const auto invalid = CalculateCenter({ Actor(1, { std::numeric_limits<double>::infinity(), 0 }, {}),
            Actor(1, { 20, 20 }, {}), Actor(2, { 0.5, 1.25 }, {}) });
        Require(invalid.mX == 0.5 && invalid.mY == 1.25,
            "an invalid first record cannot be replaced by a duplicate ID");
    }

    void TestSixteenCarsFillFourByFour()
    {
        const auto snapshot = Uniform(16, Square(-4, 4));
        const auto result = Plan(snapshot);
        Require(!result.mBudgetExceeded && result.mAssignments.size() == 16,
            "sixteen unrestricted cars must all receive destinations");
        Require(Dimensions(result) == std::pair { 4, 4 },
            "sixteen cars must occupy an even 4x4 square rather than a radial 5x5 shape");
        RequireValid(snapshot, result);
        // 唯一性加上 4x4 包络的 16 个格，证明包络内部没有空洞。
    }

    void TestNonSquareCountsAndInterior()
    {
        for (const auto [count, width, height] : { std::tuple { 6, 2, 3 }, std::tuple { 10, 3, 4 }, std::tuple { 15, 4, 4 } })
        {
            const auto snapshot = Uniform(count, Square(-4, 4));
            const auto result = Plan(snapshot);
            const auto [actualWidth, actualHeight] = Dimensions(result);
            Require(result.mAssignments.size() == static_cast<std::size_t>(count) &&
                std::min(actualWidth, actualHeight) == width && std::max(actualWidth, actualHeight) == height,
                "non-square selections must use a compact nearly-square envelope");
            RequireValid(snapshot, result);
        }
        const auto interior = Plan(Uniform(8, Square(-1, 1)));
        Require(std::any_of(interior.mAssignments.begin(), interior.mAssignments.end(),
            [](const Assignment& assignment) { return assignment.mDestination == Cell { 0, 0, false }; }),
            "the interior must fill before a closer-to-actor boundary cell");
        const auto largeInterior = Plan(Uniform(63, Square(-4, 3)));
        const auto filledInterior = std::count_if(largeInterior.mAssignments.begin(), largeInterior.mAssignments.end(),
            [](const Assignment& assignment)
            {
                return assignment.mDestination.mX > -4 && assignment.mDestination.mX < 3 &&
                    assignment.mDestination.mY > -4 && assignment.mDestination.mY < 3;
            });
        Require(largeInterior.mAssignments.size() == 63 && filledInterior == 36,
            "large formations must fill every interior cell without cubic movement refinement");
    }

    void TestObstaclesAndRestrictedActors()
    {
        auto cells = Square(-3, 3);
        cells.erase(std::remove(cells.begin(), cells.end(), Cell { 0, 0, false }), cells.end());
        const auto obstacleSnapshot = Uniform(16, cells);
        const auto obstacleResult = Plan(obstacleSnapshot);
        Require(obstacleResult.mAssignments.size() == 16, "an obstacle must expand the envelope rather than strand a car");
        RequireValid(obstacleSnapshot, obstacleResult);

        Snapshot restricted { {}, {
            Actor(1, {}, { { 0, 0, false }, { 1, 0, false } }),
            Actor(2, {}, { { 0, 0, false } }),
            Actor(3, {}, { { 0, 0, true } }),
            Actor(4, {}, {}) } };
        const auto result = Plan(restricted);
        Require(result.mAssignments.size() == 3 && result.mUnassignedActors == std::vector<ActorId> { 4 },
            "maximum matching must repair a greedy conflict and treat bridge layers separately");
        Require(result.mAssignments[0].mDestination == Cell { 1, 0, false } &&
            result.mAssignments[1].mDestination == Cell { 0, 0, false } &&
            result.mAssignments[2].mDestination == Cell { 0, 0, true },
            "restricted actor and bridge actor must keep their sole legal destinations");
        RequireValid(restricted, result);
    }

    void TestMovementAndStableInputs()
    {
        const std::vector<Cell> cells { { 0, 0, false }, { 1, 0, false } };
        Snapshot snapshot { { 0.5, 0 }, { Actor(9, { 1, 0 }, cells), Actor(4, { 0, 0 }, cells) } };
        const auto first = Plan(snapshot);
        Require(first.mAssignments == std::vector<Assignment> { { 4, cells[0] }, { 9, cells[1] } },
            "movement refinement should preserve a zero-distance matching");
        std::reverse(snapshot.mActors.begin(), snapshot.mActors.end());
        for (auto& actor : snapshot.mActors)
        {
            std::reverse(actor.mCandidates.begin(), actor.mCandidates.end());
            actor.mCandidates.push_back(actor.mCandidates.front());
        }
        snapshot.mActors.push_back(Actor(4, { 100, 100 }, { { 100, 100, false } }));
        snapshot.mActors.push_back(Actor(0, {}, { { 100, 100, false } }));
        const auto second = Plan(snapshot);
        Require(second.mAssignments == first.mAssignments && second.mUnassignedActors == first.mUnassignedActors,
            "actor order, candidate order, repeated cells and later duplicate IDs must not change the plan");
    }

    struct BruteResult
    {
        std::size_t mCount = 0;
        bool mUsesCenter = false;
    };

    void Enumerate(const Snapshot& snapshot, std::size_t actor, std::vector<Cell>& occupied, BruteResult& best)
    {
        if (actor == snapshot.mActors.size())
        {
            const bool usesCenter = std::find(occupied.begin(), occupied.end(), Cell { 0, 0, false }) != occupied.end();
            if (occupied.size() > best.mCount || (occupied.size() == best.mCount && usesCenter))
                best = { occupied.size(), usesCenter };
            return;
        }
        Enumerate(snapshot, actor + 1, occupied, best);
        for (const auto& cell : snapshot.mActors[actor].mCandidates)
        {
            if (std::find(occupied.begin(), occupied.end(), cell) != occupied.end()) continue;
            occupied.push_back(cell);
            Enumerate(snapshot, actor + 1, occupied, best);
            occupied.pop_back();
        }
    }

    void TestTinyGraphsAgainstEnumeration()
    {
        const std::vector<Cell> cells { { -1, -1, false }, { 0, 0, false }, { 1, 1, false } };
        // 3 车 x 3 格共有 512 个图；穷举独立验证最大数量及中心内部填充。
        for (unsigned mask = 0; mask < 512; ++mask)
        {
            Snapshot snapshot;
            for (unsigned actor = 0; actor < 3; ++actor)
            {
                std::vector<Cell> candidates;
                for (unsigned cell = 0; cell < 3; ++cell)
                    if (mask & (1U << (actor * 3 + cell))) candidates.push_back(cells[cell]);
                snapshot.mActors.push_back(Actor(actor + 1, { 2, 2 }, std::move(candidates)));
            }
            BruteResult expected;
            std::vector<Cell> occupied;
            Enumerate(snapshot, 0, occupied, expected);
            const auto actual = Plan(snapshot);
            Require(!actual.mBudgetExceeded && actual.mAssignments.size() == expected.mCount,
                "tiny graph cardinality must equal exhaustive matching");
            const bool usesCenter = std::any_of(actual.mAssignments.begin(), actual.mAssignments.end(),
                [](const Assignment& assignment) { return assignment.mDestination == Cell { 0, 0, false }; });
            Require(usesCenter == expected.mUsesCenter, "a usable interior center must not be left empty");
            RequireValid(snapshot, actual);
        }
    }

    void TestBudgetAndCoordinateLimits()
    {
        const auto tooManyActors = Plan(Uniform(257, { { 0, 0, false } }));
        Require(tooManyActors.mBudgetExceeded && tooManyActors.mAssignments.empty() &&
            tooManyActors.mUnassignedActors.size() == 257,
            "an oversized selection must reject the whole group without truncation");
        const auto tooManyEdges = Plan(Uniform(65, std::vector<Cell>(4096, Cell {})));
        Require(tooManyEdges.mBudgetExceeded && tooManyEdges.mAssignments.empty() &&
            tooManyEdges.mUnassignedActors.size() == 65,
            "an oversized raw candidate graph must reject the whole group");
        const auto tooManyCandidates = Plan(Uniform(1, std::vector<Cell>(4097, Cell {})));
        Require(tooManyCandidates.mBudgetExceeded, "per-actor candidate work must be bounded");

        const auto invalidCenter = Plan(Uniform(1, { {} }, { std::numeric_limits<double>::quiet_NaN(), 0 }));
        Require(invalidCenter.mAssignments.empty() && !invalidCenter.mBudgetExceeded,
            "non-finite centers must fail without converting to integer coordinates");
        const auto invalidActor = Plan({ {}, { Actor(7, { 1e100, 0 }, { {} }) } });
        Require(invalidActor.mAssignments.empty() && invalidActor.mUnassignedActors == std::vector<ActorId> { 7 },
            "unrepresentable actor positions must not enter distance calculations");

        constexpr int LOW = std::numeric_limits<std::int32_t>::min();
        constexpr int HIGH = std::numeric_limits<std::int32_t>::max();
        const Snapshot extremes { {}, { Actor(1, { static_cast<double>(LOW), static_cast<double>(LOW) }, { { LOW, LOW, false } }),
            Actor(2, { static_cast<double>(HIGH), static_cast<double>(HIGH) }, { { HIGH, HIGH, false } }) } };
        const auto result = Plan(extremes);
        Require(!result.mBudgetExceeded && result.mAssignments.size() == 2,
            "full int32 coordinate differences and envelope areas must not overflow");
        RequireValid(extremes, result);
    }

    void TestFull256ActorFixture()
    {
        // 256 x 1024 = 262144 条边，覆盖适配器最大规模；也是独立测量用的固定夹具。
        const auto snapshot = Uniform(256, Square(-16, 15));
        const auto result = Plan(snapshot);
        Require(!result.mBudgetExceeded && result.mAssignments.size() == 256 &&
            Dimensions(result) == std::pair { 16, 16 },
            "the maximum supported dense fixture must retain all cars in a 16x16 square");
        RequireValid(snapshot, result);
    }

    struct OccupancyStats
    {
        std::size_t mSelfMatched = 0;
        std::size_t mCycles = 0;
    };

    OccupancyStats CountOccupancyDependencies(const Snapshot& snapshot, const PlanResult& result)
    {
        OccupancyStats stats;
        std::vector<int> next(snapshot.mActors.size(), -1);
        for (const auto& assignment : result.mAssignments)
        {
            const auto actor = std::find_if(snapshot.mActors.begin(), snapshot.mActors.end(),
                [&](const ActorSnapshot& value) { return value.mId == assignment.mActor; });
            const auto occupant = std::find_if(snapshot.mActors.begin(), snapshot.mActors.end(),
                [&](const ActorSnapshot& value) { return value.mCurrentCell == assignment.mDestination; });
            if (actor == occupant) ++stats.mSelfMatched;
            else if (occupant != snapshot.mActors.end())
                next[actor - snapshot.mActors.begin()] = static_cast<int>(occupant - snapshot.mActors.begin());
        }
        std::vector<int> color(next.size(), 0);
        for (std::size_t actor = 0; actor < next.size(); ++actor)
        {
            int current = static_cast<int>(actor);
            std::vector<int> path;
            while (current != -1 && color[current] == 0)
            {
                color[current] = 1;
                path.push_back(current);
                current = next[current];
            }
            if (current != -1 && color[current] == 1) ++stats.mCycles;
            for (const int member : path) color[member] = 2;
        }
        return stats;
    }

    Snapshot OccupiedRings(int width, int height, bool partlyScattered)
    {
        const int lowX = -width / 2;
        const int lowY = -height / 2;
        const int highX = lowX + width - 1;
        const int highY = lowY + height - 1;
        std::vector<Cell> slots;
        std::vector<std::pair<Cell, Cell>> occupants;
        for (int depth = 0; depth < (std::min(width, height) + 1) / 2; ++depth)
        {
            const int x0 = lowX + depth, x1 = highX - depth;
            const int y0 = lowY + depth, y1 = highY - depth;
            std::vector<Cell> ring;
            for (int x = x0; x <= x1; ++x) ring.push_back({ x, y0, false });
            for (int y = y0 + 1; y <= y1; ++y) ring.push_back({ x1, y, false });
            if (y1 > y0)
                for (int x = x1 - 1; x >= x0; --x) ring.push_back({ x, y1, false });
            if (x1 > x0)
                for (int y = y1 - 1; y > y0; --y) ring.push_back({ x0, y, false });
            if (width == 12 && depth == 0)
            {
                ring.erase(std::remove_if(ring.begin(), ring.end(), [&](const Cell& cell)
                    { return (cell.mX == x0 || cell.mX == x1) && (cell.mY == y0 || cell.mY == y1); }), ring.end());
            }
            for (std::size_t index = 0; index < ring.size(); ++index)
                occupants.push_back({ ring[index], ring[(index + 1) % ring.size()] });
            slots.insert(slots.end(), ring.begin(), ring.end());
        }
        const auto depth = [&](const Cell& cell)
            { return std::min({ cell.mX - lowX, cell.mY - lowY, highX - cell.mX, highY - cell.mY }); };
        std::sort(slots.begin(), slots.end(), [&](const Cell& left, const Cell& right)
        {
            if (depth(left) != depth(right)) return depth(left) > depth(right);
            return std::tie(left.mX, left.mY) < std::tie(right.mX, right.mY);
        });
        Snapshot snapshot;
        for (std::size_t index = 0; index < slots.size(); ++index)
        {
            auto current = std::find_if(occupants.begin(), occupants.end(),
                [&](const auto& occupant) { return occupant.first == slots[index]; })->second;
            if (partlyScattered && index % 5 == 0)
            {
                const int offset = 40 + static_cast<int>(index);
                current = { index % 2 ? offset : -offset, index % 2 ? 40 : -40, false };
            }
            auto actor = Actor(index + 1, { static_cast<double>(current.mX), static_cast<double>(current.mY) }, slots);
            actor.mCurrentCell = current;
            snapshot.mActors.push_back(std::move(actor));
        }
        snapshot.mCenter = CalculateCenter(snapshot.mActors);
        return snapshot;
    }

    void TestOccupiedFinalSlotsDoNotCreateCycles()
    {
        for (const auto [width, height, count] : {
            std::tuple { 4, 4, 16 }, std::tuple { 8, 8, 64 }, std::tuple { 12, 11, 128 } })
        {
            for (const bool partlyScattered : { false, true })
            {
                const auto snapshot = OccupiedRings(width, height, partlyScattered);
                const auto result = Plan(snapshot);
                const auto stats = CountOccupancyDependencies(snapshot, result);
                const auto outside = partlyScattered ? (count + 4) / 5 : 0;
                Require(!result.mBudgetExceeded && result.mAssignments.size() == static_cast<std::size_t>(count) &&
                    Dimensions(result) == std::pair { width, height },
                    "occupancy repair must preserve the complete compact final slot set");
                Require(stats.mSelfMatched == static_cast<std::size_t>(count - outside) && stats.mCycles == 0,
                    "all cars already inside final slots must stay put without unnecessary dependency cycles");
                RequireValid(snapshot, result);
            }
        }
    }

    void TestCurrentSlotCannotStrandRestrictedActor()
    {
        auto unrestricted = Actor(1, {}, { { 0, 0, false }, { 1, 0, false } });
        unrestricted.mCurrentCell = { 0, 0, false };
        auto restricted = Actor(2, { 4, 4 }, { { 0, 0, false } });
        restricted.mCurrentCell = { 4, 4, false };
        const Snapshot snapshot { { 0.5, 0 }, { unrestricted, restricted } };
        const auto result = Plan(snapshot);
        Require(result.mAssignments == std::vector<Assignment> {
            { 1, { 1, 0, false } }, { 2, { 0, 0, false } } },
            "a current-slot lock must roll back when it would reduce maximum assignment cardinality");
        RequireValid(snapshot, result);
    }
}

void RunAutoFormationPlannerTests()
{
    TestCenterPrecisionAndFirstIds();
    TestSixteenCarsFillFourByFour();
    TestNonSquareCountsAndInterior();
    TestObstaclesAndRestrictedActors();
    TestMovementAndStableInputs();
    TestTinyGraphsAgainstEnumeration();
    TestBudgetAndCoordinateLimits();
    TestFull256ActorFixture();
    TestOccupiedFinalSlotsDoNotCreateCycles();
    TestCurrentSlotCannotStrandRestrictedActor();
}
