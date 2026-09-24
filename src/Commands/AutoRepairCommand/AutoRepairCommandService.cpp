#include "Commands/AutoRepairCommand/AutoRepairCommandService.h"

namespace ra_commands::auto_repair
{
    namespace
    {
        // 原生修理状态可能晚于事件发送回写，30 帧内不重发同一建筑。
        constexpr std::uint32_t RETRY_INTERVAL_FRAMES = 30;
    }

    AutoRepairCommandService::AutoRepairCommandService(IAutoRepairGamePort& game)
        : mGame(game)
    {
    }

    void AutoRepairCommandService::OnHotkey()
    {
        if (!SyncSession())
        {
            return;
        }
        mEnabled = !mEnabled;
    }

    void AutoRepairCommandService::OnGameFrame()
    {
        if (!SyncSession() || !mEnabled)
        {
            return;
        }

        Snapshot snapshot;
        if (!mGame.CaptureSnapshot(snapshot) || snapshot.LocalOwner == 0)
        {
            return;
        }

        const auto frame = mLastObservedFrame;
        for (const auto& building : snapshot.Buildings)
        {
            if (building.Id.Address == 0 || building.Id.UniqueId == 0 ||
                building.Owner != snapshot.LocalOwner ||
                !building.IsDamaged || building.IsBeingRepaired || !building.CanBeRepaired)
            {
                continue;
            }

            const auto previous = mLastAttemptFrames.find(building.Id);
            if (previous != mLastAttemptFrames.end() &&
                frame - previous->second < RETRY_INTERVAL_FRAMES)
            {
                continue;
            }

            if (mGame.GetNativeFreeSlots() < MIN_NATIVE_FREE_SLOTS)
            {
                break;
            }

            // 适配器在调用 Repair() 前再次检查对象身份、条件和剩余槽数。
            if (mGame.TryRepair(building.Id))
            {
                mLastAttemptFrames[building.Id] = frame;
            }
        }
    }

    void AutoRepairCommandService::Reset()
    {
        mEnabled = false;
        mHasSession = false;
        mSessionIdentity = 0;
        mLastObservedFrame = 0;
        mLastAttemptFrames.clear();
    }

    bool AutoRepairCommandService::IsEnabled() const noexcept
    {
        return mEnabled;
    }

    bool AutoRepairCommandService::SyncSession()
    {
        if (!mGame.IsMatchReady())
        {
            if (mHasSession)
            {
                Reset();
            }
            return false;
        }

        const auto sessionIdentity = mGame.GetSessionIdentity();
        if (sessionIdentity == 0)
        {
            if (mHasSession)
            {
                Reset();
            }
            return false;
        }

        const auto frame = mGame.GetCurrentFrame();
        if (!mHasSession || mSessionIdentity != sessionIdentity ||
            frame < mLastObservedFrame)
        {
            Reset();
            mHasSession = true;
            mSessionIdentity = sessionIdentity;
        }
        mLastObservedFrame = frame;
        return true;
    }
}
