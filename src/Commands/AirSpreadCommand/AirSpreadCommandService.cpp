#include "Commands/AirSpreadCommand/AirSpreadCommandService.h"

namespace ra_commands::air_spread
{
    namespace
    {
        void SubmitAssignments(
            IAirSpreadGamePort& game, const PlanResult& plan, AirSpreadCommandResult& result)
        {
            for (const auto& assignment : plan.Assignments)
            {
                if (game.SubmitMove(assignment.Actor, assignment.Destination))
                {
                    result.AcceptedMoves.push_back(assignment);
                }
                else
                {
                    result.RejectedMoves.push_back(assignment);
                }
            }
        }
    }

    AirSpreadCommandService::AirSpreadCommandService(IAirSpreadGamePort& game)
        : mGame(game)
    {
    }

    AirSpreadCommandResult AirSpreadCommandService::OnHotkey()
    {
        AirSpreadCommandResult result;
        AirSpreadSnapshot snapshot;
        if (!mGame.TryCaptureSnapshot(snapshot))
        {
            return result;
        }

        result.SnapshotCaptured = true;
        result.InfantryPlan = Plan(snapshot.InfantryActors, 3, snapshot.InfantryOccupancy);
        result.FlyingPlan = Plan(snapshot.FlyingActors, 1, snapshot.FlyingOccupancy);
        SubmitAssignments(mGame, result.InfantryPlan, result);
        SubmitAssignments(mGame, result.FlyingPlan, result);
        return result;
    }
}
