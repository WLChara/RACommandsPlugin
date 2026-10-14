#include "Commands/AutoFormationCommand/FormationCandidateBands.h"
#include "Commands/AutoFormationCommand/FormationLimits.h"
#include "Commands/DragFormationCommand/DragFormationPlanner.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace
{
    using namespace ra_commands::auto_formation;
    namespace drag = ra_commands::drag_formation;
    void Require(bool condition, const char* message)
    { if (!condition) throw std::runtime_error(message); }
    Snapshot Actors(std::size_t count, int kind)
    {
        Snapshot snapshot{{256, 256}, {}};
        for (std::size_t i = 0; i < count; ++i)
        {
            ActorSnapshot actor;
            actor.mId = i + 1;
            actor.mKind = kind == 1 || (kind == 2 && i % 2) ? ActorKind::Infantry : ActorKind::Vehicle;
            actor.mPlane = kind == 2 && i % 3 == 2 ? OccupancyPlane::Air : OccupancyPlane::Ground;
            actor.mCurrentCell = {180 + static_cast<int>(i % 32), 180 + static_cast<int>(i / 32),
                kind == 2 && i % 3 == 1};
            if (actor.mPlane == OccupancyPlane::Air) actor.mCurrentCell.mOnBridge = false;
            actor.mPosition = {double(actor.mCurrentCell.mX), double(actor.mCurrentCell.mY)};
            snapshot.mActors.push_back(std::move(actor));
        }
        return snapshot;
    }
    std::vector<Cell> NativeSearchCells()
    {
        std::vector<Cell> cells;
        for (int x = 224; x <= 288; ++x)
            for (int y = 224; y <= 288; ++y)
                if ((x - 256) * (x - 256) + (y - 256) * (y - 256) <= 1024)
                    cells.push_back({x, y, false});
        return cells;
    }
    std::vector<ActorProfile> Profiles(const Snapshot& snapshot)
    {
        std::vector<ActorProfile> profiles;
        for (const auto& actor : snapshot.mActors)
            profiles.push_back({actor.mId, actor.mKind, actor.mPlane, actor.mCurrentCell.mOnBridge});
        return profiles;
    }
    struct CaptureState
    {
        std::size_t mCalls = 0;
        std::map<ActorId, std::size_t> mActorCalls;
        std::map<std::pair<ActorId, std::size_t>, std::uint8_t> mQueries;
        std::set<std::pair<ActorId, std::size_t>> mAccepted;
    };
    // 用带身份的端点替代原生查询；同一格的不同 actor 可返回不同结果。
    std::uint8_t Endpoint(const ActorSnapshot& actor, Cell cell, int scenario)
    {
        if (scenario == 4 && cell.mX % 11 == 0 && cell.mY % 13 == 0) return 0;
        if (scenario == 4 && actor.mId % 7 == 0 && (cell.mX + cell.mY) % 17 == 0) return 0;
        if (actor.mKind == ActorKind::Vehicle) return 1;
        if (scenario == 1 || scenario == 2) return static_cast<std::uint8_t>(scenario);
        if (scenario == 3 && cell.mX % 11 == 0 && cell.mY % 13 == 0) return 1;
        return 3;
    }
    bool Capture(Snapshot& snapshot, const std::vector<Cell>& cells,
        const std::vector<PlaneLayout>& planes, std::uint8_t capacity,
        int scenario, CaptureState& state)
    {
        const auto bands = BuildFormationCandidateBands(snapshot, cells, planes, capacity);
        if (bands.size() != snapshot.mActors.size()) return false;
        std::map<ActorId, ActorSnapshot*> actors;
        for (auto& actor : snapshot.mActors) actors.emplace(actor.mId, &actor);
        for (const auto& band : bands)
        {
            auto& actor = *actors.at(band.mActor);
            std::size_t accepted = 0;
            for (const auto index : band.mCellIndices)
            {
                const auto key = std::pair{actor.mId, index};
                auto found = state.mQueries.find(key);
                Cell destination = cells[index];
                destination.mOnBridge = actor.mCurrentCell.mOnBridge;
                if (found == state.mQueries.end())
                {
                    if (state.mCalls >= MAX_FORMATION_ACTORS * LARGE_FORMATION_QUERIES_PER_ACTOR ||
                        state.mActorCalls[actor.mId] >= LARGE_FORMATION_QUERIES_PER_ACTOR) return false;
                    ++state.mCalls;
                    ++state.mActorCalls[actor.mId];
                    found = state.mQueries.emplace(key, Endpoint(actor, destination, scenario)).first;
                }
                const auto available = found->second;
                if (!available) continue;
                ++accepted;
                if (state.mAccepted.insert(key).second)
                {
                    actor.mCandidates.push_back(destination);
                    if (actor.mKind == ActorKind::Infantry && available < 3)
                        actor.mCapacityLimits.push_back({destination, available});
                }
                if (accepted == LARGE_FORMATION_ACCEPTED_CANDIDATES) break;
            }
            if (actor.mCandidates.empty()) return false;
        }
        return true;
    }
    bool CaptureCompact(Snapshot& snapshot, const std::vector<Cell>& cells, int scenario, CaptureState& state)
    {
        std::uint8_t capacity = 3;
        for (int pass = 0; pass < 3; ++pass)
        {
            if (!Capture(snapshot, cells, {}, capacity, scenario, state)) return false;
            auto next = capacity;
            for (const auto& actor : snapshot.mActors)
                if (actor.mKind == ActorKind::Infantry && actor.mCapacityLimits.size() == actor.mCandidates.size())
                {
                    std::uint8_t maximum = 0;
                    for (const auto& limit : actor.mCapacityLimits) maximum = std::max(maximum, limit.mAvailable);
                    next = std::min(next, maximum);
                }
            if (next >= capacity) return true;
            capacity = next;
        }
        return true;
    }
    void RequireComplete(const Snapshot& snapshot, const PlanResult& plan)
    {
        Require(!plan.mBudgetExceeded && plan.mAssignments.size() == snapshot.mActors.size()
            && plan.mUnassignedActors.empty() && HasValidReservations(plan.mAssignments),
            "real sparse candidate distribution must produce a complete legal plan");
        std::map<ActorId, const ActorSnapshot*> actors;
        for (const auto& actor : snapshot.mActors) actors.emplace(actor.mId, &actor);
        for (const auto& assignment : plan.mAssignments)
        {
            const auto* actor = actors.at(assignment.mActor);
            Require(std::find(actor->mCandidates.begin(), actor->mCandidates.end(), assignment.mDestination)
                != actor->mCandidates.end(), "planner must use individually queried candidates");
            for (const auto& limit : actor->mCapacityLimits)
                if (limit.mCell == assignment.mDestination)
                    Require(assignment.mSeat < limit.mAvailable, "sparse capture plan exceeded observed cell capacity");
        }
    }
    void TestSparseCaptureToCompletePlanner()
    {
        const auto cells = NativeSearchCells();
        for (std::size_t count : {std::size_t{512}, std::size_t{1000}})
            for (int kind : {0, 1, 2})
                for (int scenario : {0, 4})
                {
                    auto snapshot = Actors(count, kind);
                    CaptureState state;
                    const auto start = std::chrono::steady_clock::now();
                    Require(CaptureCompact(snapshot, cells, scenario, state), "sparse native-like capture failed");
                    const auto plan = Plan(snapshot);
                    RequireComplete(snapshot, plan);
                    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    std::printf("capture+plan n=%zu kind=%d obstacles=%d queries=%zu ms=%.3f\n",
                        count, kind, scenario == 4, state.mCalls, elapsed);
                    Require(state.mCalls <= count * LARGE_FORMATION_QUERIES_PER_ACTOR,
                        "sparse endpoint calls exceeded per-actor query budget");
                    if (scenario == 0) Require(state.mCalls == count * LARGE_FORMATION_ACCEPTED_CANDIDATES,
                        "ordinary flat terrain should use one 64-query band per actor");
                }
    }
    void TestInfantryDensityFallbackAndTightPacking()
    {
        const auto cells = NativeSearchCells();
        for (int scenario : {1, 2, 3})
        {
            auto snapshot = Actors(1000, 1);
            CaptureState state;
            Require(CaptureCompact(snapshot, cells, scenario, state), "infantry fallback capture exceeded query budget");
            const auto plan = Plan(snapshot);
            RequireComplete(snapshot, plan);
            std::set<std::pair<int, int>> used;
            for (const auto& assignment : plan.mAssignments)
                used.emplace(assignment.mDestination.mX, assignment.mDestination.mY);
            const std::size_t demand = scenario == 1 ? 1000 : scenario == 2 ? 500 : 334;
            Require(used.size() == demand, "few reduced-capacity cells must preserve compact three-seat packing");
            Require(state.mCalls <= 256000, "capacity fallback must share the 256-per-actor query budget");
            if (scenario == 3) Require(state.mCalls == 64000,
                "one reduced-capacity candidate must not trigger a full dispersed recapture");
            std::printf("capture+plan 1000 infantry cellCapacity=%d queries=%zu physical=%zu\n",
                scenario, state.mCalls, used.size());
        }
    }
    void TestDragFootprintAndSnapshotSpecificQueries()
    {
        for (const std::uint8_t capacity : {std::uint8_t{3}, std::uint8_t{2}, std::uint8_t{1}})
        {
            auto snapshot = Actors(1000, 1);
            const auto layout = drag::BuildLayout({256, 256}, {267, 289}, Profiles(snapshot), 0.4, 0.3, capacity);
            const auto cells = drag::BuildPlacementCells(layout);
            CaptureState state;
            Require(Capture(snapshot, cells, layout.mPlanes, capacity, capacity, state), "drag footprint capture failed");
            RequireComplete(snapshot, drag::PlanPlacement(layout, snapshot));
        }
        auto mixed = Actors(1000, 2);
        const auto layout = drag::BuildLayout({256, 256}, {267, 289}, Profiles(mixed));
        const auto cells = drag::BuildPlacementCells(layout);
        CaptureState state;
        Require(Capture(mixed, cells, layout.mPlanes, 3, 4, state), "mixed rotated sparse capture failed");
        RequireComplete(mixed, drag::PlanPlacement(layout, mixed));
        auto invalid = mixed;
        invalid.mActors.back().mId = invalid.mActors.front().mId;
        Require(BuildFormationCandidateBands(invalid, cells, layout.mPlanes, 3).empty(),
            "invalid sparse-capture identities must reject the whole graph");
        const auto bands = BuildFormationCandidateBands(Actors(1000, 2), cells, layout.mPlanes, 3);
        Require(bands.size() == 1000, "sparse bands must include every actor");
        for (const auto& band : bands) Require(band.mCellIndices.size() <= 256,
            "candidate distribution must not create a dense native-query fallback");
    }
}
void RunFormationCandidateBandsTests()
{
    TestSparseCaptureToCompletePlanner();
    TestInfantryDensityFallbackAndTightPacking();
    TestDragFootprintAndSnapshotSpecificQueries();
}
