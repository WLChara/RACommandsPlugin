#include "Commands/AutoFormationCommand/AutoFormationPlanner.h"
#include "Commands/AutoFormationCommand/FormationCellPacking.h"
#include "Commands/AutoFormationCommand/FormationLimits.h"
#include "Commands/AutoFormationCommand/LargeFormationMatching.h"
#include "Commands/DragFormationCommand/DragFormationPlanner.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace ra_commands::auto_formation;
    namespace drag = ra_commands::drag_formation;
    void Require(bool condition, const char* message)
    { if (!condition) throw std::runtime_error(message); }
    std::vector<Cell> Square(int low, int size, bool bridge = false)
    {
        std::vector<Cell> cells;
        for (int x = low; x < low + size; ++x)
            for (int y = low; y < low + size; ++y) cells.push_back({x, y, bridge});
        return cells;
    }
    Snapshot Team(std::size_t count, int mode)
    {
        Snapshot snapshot{{256, 256}, {}};
        for (std::size_t index = 0; index < count; ++index)
        {
            ActorSnapshot actor;
            actor.mId = index + 1;
            actor.mKind = mode == 1 || (mode == 2 && index % 2) ? ActorKind::Infantry : ActorKind::Vehicle;
            actor.mPlane = mode == 2 && index % 3 == 2 ? OccupancyPlane::Air : OccupancyPlane::Ground;
            actor.mCurrentCell = {180 + static_cast<int>(index % 32), 180 + static_cast<int>(index / 32),
                mode == 2 && index % 3 == 1};
            if (actor.mPlane == OccupancyPlane::Air) actor.mCurrentCell.mOnBridge = false;
            actor.mPosition = {double(actor.mCurrentCell.mX), double(actor.mCurrentCell.mY)};
            actor.mCandidates = Square(240, 32, actor.mCurrentCell.mOnBridge);
            snapshot.mActors.push_back(std::move(actor));
        }
        return snapshot;
    }
    void Complete(const Snapshot& snapshot, const PlanResult& plan)
    {
        Require(!plan.mBudgetExceeded && plan.mAssignments.size() == snapshot.mActors.size()
            && plan.mUnassignedActors.empty() && HasValidReservations(plan.mAssignments),
            "large plan must include every actor exactly once with valid reservations");
        std::map<ActorId, const ActorSnapshot*> actors;
        for (const auto& actor : snapshot.mActors) actors.emplace(actor.mId, &actor);
        for (const auto& assignment : plan.mAssignments)
        {
            const auto* actor = actors.at(assignment.mActor);
            Require(actor->mKind == assignment.mKind && actor->mPlane == assignment.mPlane
                && actor->mCurrentCell.mOnBridge == assignment.mDestination.mOnBridge,
                "large assignment must preserve actor class and resource plane");
            Require(std::find(actor->mCandidates.begin(), actor->mCandidates.end(), assignment.mDestination)
                != actor->mCandidates.end(), "large assignment must use a legal actor candidate");
            for (const auto& limit : actor->mCapacityLimits)
                if (limit.mCell == assignment.mDestination)
                    Require(assignment.mSeat < limit.mAvailable, "large infantry seat exceeds observed capacity");
        }
    }
    std::vector<ActorProfile> Profiles(const Snapshot& snapshot)
    {
        std::vector<ActorProfile> profiles;
        for (const auto& actor : snapshot.mActors)
            profiles.push_back({actor.mId, actor.mKind, actor.mPlane, actor.mCurrentCell.mOnBridge});
        return profiles;
    }
    void UsePlacementCandidates(Snapshot& snapshot, const drag::Layout& layout)
    {
        const auto candidates = drag::BuildPlacementCells(layout);
        Require(!candidates.empty(), "large drag layout must provide placement candidates");
        for (auto& actor : snapshot.mActors)
        {
            actor.mCandidates = candidates;
            for (auto& cell : actor.mCandidates) cell.mOnBridge = actor.mCurrentCell.mOnBridge;
        }
    }
    void TestUniformCompleteAndPermutation()
    {
        for (const auto count : {std::size_t{512}, std::size_t{1000}})
            for (int mode : {0, 1, 2})
            {
                auto snapshot = Team(count, mode);
                const auto autoPlan = Plan(snapshot);
                Complete(snapshot, autoPlan);
                auto reordered = snapshot;
                std::reverse(reordered.mActors.begin(), reordered.mActors.end());
                for (auto& actor : reordered.mActors) std::reverse(actor.mCandidates.begin(), actor.mCandidates.end());
                Require(Plan(reordered).mAssignments == autoPlan.mAssignments,
                    "actor and candidate permutation must preserve deterministic large assignments");
                const auto layout = drag::BuildLayout({256, 256}, {267, 289}, Profiles(snapshot));
                Require(layout.mValid && layout.mSlots.size() == count, "large drag preview must retain the whole team");
                UsePlacementCandidates(snapshot, layout);
                const auto dragPlan = drag::PlanPlacement(layout, snapshot);
                Complete(snapshot, dragPlan);
                std::reverse(snapshot.mActors.begin(), snapshot.mActors.end());
                const auto reorderedLayout = drag::BuildLayout({256, 256}, {267, 289}, Profiles(snapshot));
                Require(drag::PlanPlacement(reorderedLayout, snapshot).mAssignments == dragPlan.mAssignments,
                    "frozen actor input permutation must preserve physical drag assignments");
            }
    }
    void TestRestrictedMatchingAndCapacityFallback()
    {
        auto vehicles = Team(1000, 0);
        vehicles.mActors.back().mCandidates = {{240, 240, false}};
        Complete(vehicles, Plan(vehicles));

        auto infantry = Team(1000, 1);
        for (auto& actor : infantry.mActors)
            for (const auto cell : actor.mCandidates) actor.mCapacityLimits.push_back({cell, 2});
        const auto compact = Plan(infantry);
        Complete(infantry, compact);
        const auto layout = drag::BuildLayout({256, 256}, {256, 289}, Profiles(infantry), 0.4, 0.3, 2);
        UsePlacementCandidates(infantry, layout);
        for (auto& actor : infantry.mActors)
        {
            actor.mCapacityLimits.clear();
            for (const auto cell : actor.mCandidates) actor.mCapacityLimits.push_back({cell, 2});
        }
        Complete(infantry, drag::PlanPlacement(layout, infantry));
        const auto failedThree = PlanLargeCellPacking(infantry,
            drag::BuildLayout({256, 256}, {256, 289}, Profiles(infantry)).mPlanes, 1.5, 3);
        Require(failedThree.mAssignments.empty() && failedThree.mUnassignedActors.size() == 1000,
            "three-seat bundles must reject cells with only two seats");

        auto independent = Team(512, 1);
        const auto independentLayout = drag::BuildLayout({256, 256}, {256, 289}, Profiles(independent), 0.4, 0.3, 1);
        std::set<Cell, bool(*)(const Cell&, const Cell&)> chosen([](const Cell& a, const Cell& b)
            { return a.mX != b.mX ? a.mX < b.mX : a.mY < b.mY; });
        for (std::size_t i = 0; i < independent.mActors.size(); ++i)
        {
            const auto center = independentLayout.mPlanes.front().mCellCenters[i];
            const Cell cell{static_cast<int>(std::round(center.mX)), static_cast<int>(std::round(center.mY)), false};
            Require(chosen.insert(cell).second, "fixture singleton cells must be unique");
            independent.mActors[i].mCandidates = {cell};
        }
        Complete(independent, Plan(independent));
    }
    void TestWholeTeamRejection()
    {
        const auto maximum = Team(MAX_FORMATION_ACTORS, 0);
        Complete(maximum, Plan(maximum));
        auto missing = Team(1000, 2);
        missing.mActors.back().mCandidates.clear();
        const auto noPlan = Plan(missing);
        Require(noPlan.mAssignments.empty() && noPlan.mUnassignedActors.size() == 1000,
            "one missing actor must reject the entire large team");
        auto oversized = Team(MAX_FORMATION_ACTORS + 1, 0);
        const auto tooMany = Plan(oversized);
        Require(tooMany.mBudgetExceeded && tooMany.mAssignments.empty()
            && tooMany.mUnassignedActors.size() == MAX_FORMATION_ACTORS + 1,
            "oversized teams must reject without truncation");
        auto badEdges = Team(1000, 0);
        badEdges.mActors.front().mCandidates.resize(MAX_FORMATION_CANDIDATES_PER_ACTOR + 1);
        const auto edges = Plan(badEdges);
        Require(edges.mBudgetExceeded && edges.mAssignments.empty() && edges.mUnassignedActors.size() == 1000,
            "oversized candidate input must reject the whole team");
        auto heavy = Team(1024, 0);
        for (auto& actor : heavy.mActors) actor.mCandidates = Square(224, 64);
        heavy.mActors.back().mCandidates.pop_back();
        const auto largeLayout = drag::BuildLayout({256, 256}, {256, 289}, Profiles(heavy));
        const auto exhausted = PlanLargeCellPacking(heavy, largeLayout.mPlanes, 32, 3);
        Require(exhausted.mBudgetExceeded && exhausted.mAssignments.empty()
            && exhausted.mUnassignedActors.size() == 1024,
            "flow or work budget exhaustion must reject every actor without partial assignments");
    }

    void TestRestrictedFlowAgainstExhaustiveMatching()
    {
        const std::vector<Cell> cells{{10, 10, false}, {11, 10, false}, {12, 10, false}};
        const std::vector<PlaneLayout> layout{{OccupancyPlane::Ground, false, {{10, 10}, {11, 10}, {12, 10}}}};
        for (unsigned mask = 0; mask < 512; ++mask)
        {
            Snapshot snapshot{{11, 10}, {}};
            for (unsigned row = 0; row < 3; ++row)
            {
                ActorSnapshot actor;
                actor.mId = row + 1;
                actor.mPosition = {9, 9};
                actor.mCurrentCell = {9, 9, false};
                for (unsigned column = 0; column < 3; ++column)
                    if (mask & (1u << (row * 3 + column))) actor.mCandidates.push_back(cells[column]);
                snapshot.mActors.push_back(std::move(actor));
            }
            std::array<unsigned, 3> permutation{0, 1, 2};
            bool feasible = false;
            do
            {
                bool complete = true;
                for (unsigned row = 0; row < 3; ++row)
                    complete = complete && (mask & (1u << (row * 3 + permutation[row]))) != 0;
                feasible = feasible || complete;
            } while (std::next_permutation(permutation.begin(), permutation.end()));
            const auto plan = PlanLargeCellPacking(snapshot, layout, 0.1, 3);
            Require(!plan.mBudgetExceeded && (plan.mAssignments.size() == 3) == feasible,
                "large restricted flow must agree with exhaustive matching, including alternating paths");
            if (feasible) Complete(snapshot, plan);
            else Require(plan.mAssignments.empty() && plan.mUnassignedActors.size() == 3,
                "incomplete restricted flow must not publish a partial team");
            std::reverse(snapshot.mActors.begin(), snapshot.mActors.end());
            Require(PlanLargeCellPacking(snapshot, layout, 0.1, 3).mAssignments == plan.mAssignments,
                "restricted-flow actor permutation must preserve assignments");
        }
    }
}

void RunLargeFormationMatchingTests()
{
    TestUniformCompleteAndPermutation();
    TestRestrictedMatchingAndCapacityFallback();
    TestWholeTeamRejection();
    TestRestrictedFlowAgainstExhaustiveMatching();
}
