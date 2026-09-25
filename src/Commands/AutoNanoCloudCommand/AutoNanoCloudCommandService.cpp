#include "Commands/AutoNanoCloudCommand/AutoNanoCloudCommandService.h"

#include <vector>

namespace ra_commands::auto_nano_cloud
{
    namespace
    {
        bool WasAccepted(commands::ClickedMissionEnqueueResult result)
        {
            return result == commands::ClickedMissionEnqueueResult::Enqueued ||
                result == commands::ClickedMissionEnqueueResult::Duplicate;
        }
    }

    AutoNanoCloudCommandService::AutoNanoCloudCommandService(
        IAutoNanoCloudGamePort& game, commands::ClickedMissionDispatcher& dispatcher)
        : mGame(game), mDispatcher(dispatcher)
    {
    }

    void AutoNanoCloudCommandService::OnHotkey()
    {
        if (!mDispatcher.IsSessionActive())
        {
            Reset();
            return;
        }
        if (mEpoch != mDispatcher.Epoch())
        {
            Reset();
            mEpoch = mDispatcher.Epoch();
        }

        Snapshot snapshot;
        if (!mGame.CaptureSnapshot(mRetainedVictim, snapshot))
        {
            return;
        }
        if (mRetainedVictim && !snapshot.RetainedVictim)
        {
            mRetainedVictim.reset();
        }
        const auto plan = Plan(snapshot);
        if (!plan)
        {
            return;
        }

        commands::ClickedMissionIntent stopIntent;
        if (!mGame.MakeStopIntent(plan->Victim, mEpoch, stopIntent))
        {
            return;
        }
        std::vector<commands::ClickedMissionIntent> attackIntents;
        attackIntents.reserve(plan->Hunters.size());
        for (const UnitId hunter : plan->Hunters)
        {
            commands::ClickedMissionIntent attackIntent;
            if (mGame.MakeAttackIntent(hunter, plan->Victim, mEpoch, attackIntent))
            {
                attackIntents.push_back(attackIntent);
            }
        }
        if (attackIntents.empty())
        {
            return;
        }

        if (mRetainedVictim && *mRetainedVictim != plan->Victim)
        {
            mDispatcher.CancelByProducer(commands::ClickedMissionProducer::AutoNanoCloud);
            mRetainedVictim.reset();
        }
        const auto stopResult = mDispatcher.Submit(stopIntent);
        if (!WasAccepted(stopResult))
        {
            return;
        }

        bool hasAttack = false;
        for (const auto& attackIntent : attackIntents)
        {
            hasAttack |= WasAccepted(mDispatcher.Submit(attackIntent));
        }
        if (!hasAttack)
        {
            if (stopResult == commands::ClickedMissionEnqueueResult::Enqueued)
            {
                mDispatcher.CancelByProducerAndActor(
                    commands::ClickedMissionProducer::AutoNanoCloud, stopIntent.Actor);
            }
            return;
        }

        if (!plan->UsesRetainedVictim && !mGame.DeselectAndUngroup(plan->Victim))
        {
            mDispatcher.CancelByProducer(commands::ClickedMissionProducer::AutoNanoCloud);
            return;
        }
        mRetainedVictim = plan->Victim;
    }

    void AutoNanoCloudCommandService::Reset()
    {
        mDispatcher.CancelByProducer(commands::ClickedMissionProducer::AutoNanoCloud);
        mRetainedVictim.reset();
        mEpoch = 0;
    }
}
