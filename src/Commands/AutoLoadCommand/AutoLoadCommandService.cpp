#include "Commands/AutoLoadCommand/AutoLoadCommandService.h"

#include "Commands/AutoLoadCommand/AutoLoadPlanner.h"

#include <algorithm>
#include <vector>

namespace ra_commands::autoload
{
    AutoLoadCommandService::AutoLoadCommandService(
        IAutoLoadGamePort& game,
        commands::ClickedMissionDispatcher& dispatcher)
        : mGame(game), mDispatcher(dispatcher)
    {
    }

    void AutoLoadCommandService::OnHotkey()
    {
        if (!mDispatcher.IsSessionActive())
        {
            return;
        }

        Snapshot snapshot;
        if (!mGame.CaptureSnapshot(snapshot))
        {
            return;
        }

        const auto pairs = Plan(snapshot);
        std::vector<UnitId> deselect;
        for (const auto& pair : pairs)
        {
            commands::ClickedMissionIntent intent;
            if (!mGame.MakeEnterIntent(
                    pair.Passenger, pair.Transport, mDispatcher.Epoch(), intent))
            {
                continue;
            }

            const auto result = mDispatcher.Submit(intent);
            if (result != commands::ClickedMissionEnqueueResult::Enqueued &&
                result != commands::ClickedMissionEnqueueResult::Duplicate)
            {
                continue;
            }

            if (std::find(deselect.begin(), deselect.end(), pair.Passenger) == deselect.end())
            {
                deselect.push_back(pair.Passenger);
            }
            if (std::find(deselect.begin(), deselect.end(), pair.Transport) == deselect.end())
            {
                deselect.push_back(pair.Transport);
            }
        }

        for (const auto id : deselect)
        {
            mGame.Deselect(id);
        }
    }

}
