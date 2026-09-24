#include "Commands/AirSpreadCommand/AirSpreadCommandService.h"

#include <stdexcept>
#include <unordered_set>
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

    class MockAirSpreadPort final : public IAirSpreadGamePort
    {
    public:
        AirSpreadSnapshot Snapshot;
        bool CaptureSucceeds = true;
        mutable int CaptureCount = 0;
        std::unordered_set<ActorId> RejectedActors;
        std::vector<std::pair<ActorId, Cell>> Attempts;

        bool TryCaptureSnapshot(AirSpreadSnapshot& outSnapshot) const override
        {
            ++CaptureCount;
            if (!CaptureSucceeds)
            {
                return false;
            }
            outSnapshot = Snapshot;
            return true;
        }

        bool SubmitMove(ActorId actor, Cell destination) override
        {
            Attempts.emplace_back(actor, destination);
            return !RejectedActors.contains(actor);
        }
    };

    void TestTwoGroupsRespectCapacitiesAndMayOverlap()
    {
        const Cell near{10, 10};
        const Cell next{11, 10};
        MockAirSpreadPort port;
        port.Snapshot.InfantryActors = {
            Actor(1, ActorKind::FlyingInfantry, {near, next}),
            Actor(2, ActorKind::FlyingInfantry, {near, next}),
            Actor(3, ActorKind::FlyingInfantry, {near, next}),
            Actor(4, ActorKind::FlyingInfantry, {near, next})};
        port.Snapshot.FlyingActors = {
            Actor(5, ActorKind::FlyingUnit, {near, next}),
            Actor(6, ActorKind::Aircraft, {near, next})};

        AirSpreadCommandService service(port);
        const auto result = service.OnHotkey();

        Require(result.SnapshotCaptured && port.CaptureCount == 1,
            "one hotkey should capture one snapshot");
        Require(result.InfantryPlan.Assignments == std::vector<Assignment>{
            {1, ActorKind::FlyingInfantry, near},
            {2, ActorKind::FlyingInfantry, near},
            {3, ActorKind::FlyingInfantry, near},
            {4, ActorKind::FlyingInfantry, next}},
            "infantry should fill three slots before using the next cell");
        Require(result.FlyingPlan.Assignments == std::vector<Assignment>{
            {5, ActorKind::FlyingUnit, near},
            {6, ActorKind::Aircraft, next}},
            "flying units and aircraft should share one slot per cell");
        Require(result.InfantryPlan.UnassignedActors.empty() &&
            result.FlyingPlan.UnassignedActors.empty(),
            "all actors should receive a cell");
        Require(result.AcceptedMoves.size() == 6 && result.RejectedMoves.empty(),
            "every planned move should be accepted by the port");
        Require(port.Attempts == std::vector<std::pair<ActorId, Cell>>{
            {1, near}, {2, near}, {3, near}, {4, next}, {5, near}, {6, next}},
            "the two groups may overlap and submit in stable plan order");
    }

    void TestInitialOccupancyAndCandidateShortage()
    {
        const Cell near{2, 3};
        const Cell next{3, 3};
        MockAirSpreadPort port;
        port.Snapshot.InfantryActors = {
            Actor(10, ActorKind::FlyingInfantry, {near, next}),
            Actor(11, ActorKind::FlyingInfantry, {near, next}),
            Actor(12, ActorKind::FlyingInfantry, {})};
        port.Snapshot.InfantryOccupancy = {{near, 2}, {next, 3}};
        port.Snapshot.FlyingActors = {
            Actor(20, ActorKind::FlyingUnit, {near, next}),
            Actor(21, ActorKind::Aircraft, {near, next})};
        port.Snapshot.FlyingOccupancy = {{near, 1}};

        AirSpreadCommandService service(port);
        const auto result = service.OnHotkey();

        Require(result.InfantryPlan.Assignments == std::vector<Assignment>{
            {10, ActorKind::FlyingInfantry, near}},
            "infantry occupancy should leave only one slot at the near cell");
        Require(result.InfantryPlan.UnassignedActors == std::vector<ActorId>{11, 12},
            "full or absent infantry candidates should remain unassigned");
        Require(result.FlyingPlan.Assignments == std::vector<Assignment>{
            {20, ActorKind::FlyingUnit, next}},
            "flying occupancy should force the first actor to the next cell");
        Require(result.FlyingPlan.UnassignedActors == std::vector<ActorId>{21},
            "the second flying actor should remain unassigned");
        Require(port.Attempts == std::vector<std::pair<ActorId, Cell>>{
            {10, near}, {20, next}},
            "unassigned actors should never be submitted");
    }

    void TestCaptureFailureAndEmptyGroups()
    {
        const Cell cell{8, 8};
        MockAirSpreadPort port;
        port.Snapshot.InfantryActors = {
            Actor(1, ActorKind::FlyingInfantry, {cell})};
        port.CaptureSucceeds = false;
        AirSpreadCommandService service(port);

        const auto failed = service.OnHotkey();
        Require(!failed.SnapshotCaptured && port.CaptureCount == 1 &&
            failed.InfantryPlan.Assignments.empty() && failed.FlyingPlan.Assignments.empty() &&
            failed.AcceptedMoves.empty() && failed.RejectedMoves.empty() && port.Attempts.empty(),
            "failed capture should neither plan nor submit");

        port.CaptureSucceeds = true;
        port.Snapshot = {};
        const auto empty = service.OnHotkey();
        Require(empty.SnapshotCaptured && port.CaptureCount == 2 &&
            empty.InfantryPlan.Assignments.empty() && empty.FlyingPlan.Assignments.empty() &&
            empty.InfantryPlan.UnassignedActors.empty() &&
            empty.FlyingPlan.UnassignedActors.empty() && port.Attempts.empty(),
            "empty groups should succeed without submissions");

        port.Snapshot.FlyingActors = {Actor(2, ActorKind::Aircraft, {cell})};
        const auto flyingOnly = service.OnHotkey();
        Require(flyingOnly.SnapshotCaptured &&
            flyingOnly.InfantryPlan.Assignments.empty() &&
            flyingOnly.FlyingPlan.Assignments == std::vector<Assignment>{
                {2, ActorKind::Aircraft, cell}} &&
            port.Attempts == std::vector<std::pair<ActorId, Cell>>{{2, cell}},
            "an empty infantry group should not suppress the flying group");
    }

    void TestSubmitFailuresAreReportedAndDoNotStopLaterMoves()
    {
        const Cell cell{4, 5};
        MockAirSpreadPort port;
        port.Snapshot.InfantryActors = {
            Actor(1, ActorKind::FlyingInfantry, {cell}),
            Actor(2, ActorKind::FlyingInfantry, {cell})};
        port.Snapshot.FlyingActors = {
            Actor(3, ActorKind::Aircraft, {cell})};
        port.RejectedActors = {1, 3};
        AirSpreadCommandService service(port);

        const auto result = service.OnHotkey();

        Require(port.Attempts == std::vector<std::pair<ActorId, Cell>>{
            {1, cell}, {2, cell}, {3, cell}},
            "submission failure should not stop later attempts or the next group");
        Require(result.AcceptedMoves == std::vector<Assignment>{
            {2, ActorKind::FlyingInfantry, cell}},
            "accepted moves should include only port successes");
        Require(result.RejectedMoves == std::vector<Assignment>{
            {1, ActorKind::FlyingInfantry, cell},
            {3, ActorKind::Aircraft, cell}},
            "rejected moves should retain actor, kind and destination");
        Require(result.InfantryPlan.Assignments.size() == 2 &&
            result.FlyingPlan.Assignments.size() == 1,
            "submission failures should not change the two completed plans");
    }
}

void RunAirSpreadServiceTests()
{
    TestTwoGroupsRespectCapacitiesAndMayOverlap();
    TestInitialOccupancyAndCandidateShortage();
    TestCaptureFailureAndEmptyGroups();
    TestSubmitFailuresAreReportedAndDoNotStopLaterMoves();
}
