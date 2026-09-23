#include "Commands/TeslaChargeCommand/TeslaChargeCommandService.h"

namespace ra_commands::tesla_charge
{
    namespace
    {
        constexpr std::uint32_t MAINTENANCE_INTERVAL = 64;
        constexpr std::uint32_t DESELECT_INTERVAL = 8;
    }

    TeslaChargeCommandService::TeslaChargeCommandService(
        ITeslaChargeGamePort& game,
        commands::ClickedMissionDispatcher& dispatcher)
        : mGame(game), mDispatcher(dispatcher)
    {
    }

    void TeslaChargeCommandService::OnHotkey()
    {
        if (!mDispatcher.IsSessionActive())
        {
            return;
        }

        if (mEpoch != mDispatcher.Epoch())
        {
            Reset();
            mEpoch = mDispatcher.Epoch();
        }

        if (mEnabled)
        {
            Reset();
            return;
        }

        mEnabled = true;
        mLastMaintenanceFrame = mGame.GetCurrentFrame();
        mLastDeselectFrame = mLastMaintenanceFrame;
        MaintainAssignments();
        DeselectAssigned();
    }

    void TeslaChargeCommandService::OnGameFrame()
    {
        if (!mDispatcher.IsSessionActive() || mEpoch != mDispatcher.Epoch())
        {
            Reset();
            mEpoch = mDispatcher.Epoch();
            return;
        }
        if (!mEnabled)
        {
            return;
        }

        const auto frame = mGame.GetCurrentFrame();
        if (frame - mLastMaintenanceFrame >= MAINTENANCE_INTERVAL)
        {
            MaintainAssignments();
            mLastMaintenanceFrame = frame;
        }
        if (frame - mLastDeselectFrame >= DESELECT_INTERVAL)
        {
            DeselectAssigned();
            mLastDeselectFrame = frame;
        }
    }

    void TeslaChargeCommandService::Reset()
    {
        mDispatcher.CancelByProducer(commands::ClickedMissionProducer::TeslaCharge);
        mAssignments.clear();
        mEnabled = false;
        mEpoch = 0;
        mLastMaintenanceFrame = 0;
        mLastDeselectFrame = 0;
    }

    bool TeslaChargeCommandService::IsEnabled() const noexcept
    {
        return mEnabled;
    }

    void TeslaChargeCommandService::MaintainAssignments()
    {
        Snapshot snapshot;
        if (!mGame.CaptureSnapshot(snapshot))
        {
            return;
        }

        mAssignments = Plan(snapshot, mAssignments);
        for (const auto& assignment : mAssignments)
        {
            if (mGame.IsTargetingTesla(assignment.Charger, assignment.Tesla))
            {
                continue;
            }

            commands::ClickedMissionIntent intent;
            if (mGame.MakeAttackIntent(assignment.Charger, assignment.Tesla,
                    mDispatcher.Epoch(), intent))
            {
                mDispatcher.Submit(intent);
            }
        }
    }

    void TeslaChargeCommandService::DeselectAssigned()
    {
        for (const auto& assignment : mAssignments)
        {
            mGame.DeselectIfSelected(assignment.Charger);
        }
    }
}
