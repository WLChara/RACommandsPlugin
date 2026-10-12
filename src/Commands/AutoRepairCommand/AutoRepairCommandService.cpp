#include "Commands/AutoRepairCommand/AutoRepairCommandService.h"

namespace ra_commands::auto_repair
{
    namespace
    {
        // 原生修理状态可能晚于事件发送回写，30 帧内不重发同一建筑。
        constexpr std::uint32_t RETRY_INTERVAL_FRAMES = 30;
        constexpr std::uint32_t MIN_FIRST_SAFE_REPAIR_DELAY_MS = 1'000;
        constexpr std::uint32_t MAX_FIRST_SAFE_REPAIR_DELAY_MS = 2'000;
        constexpr std::uint32_t MIN_NEXT_SAFE_REPAIR_DELAY_MS = 100;
        constexpr std::uint32_t MAX_NEXT_SAFE_REPAIR_DELAY_MS = 1'000;

        std::uint32_t SafeRepairDelayMs(BuildingId id, std::uint32_t frame, int health,
            std::uint32_t minDelayMs, std::uint32_t maxDelayMs)
        {
            // 用当前快照选择一次延迟，不调用游戏随机数，也不在等待期间重新抽取。
            std::uint32_t value = id.UniqueId * 0x9E3779B9u ^
                frame * 0x85EBCA6Bu ^ static_cast<std::uint32_t>(health);
            value ^= value >> 16;
            value *= 0x7FEB352Du;
            value ^= value >> 15;
            return minDelayMs + value % (maxDelayMs - minDelayMs + 1);
        }
    }

    AutoRepairCommandService::AutoRepairCommandService(IAutoRepairGamePort& game,
        const std::atomic<bool>& isSafeModeEnabled)
        : mGame(game), mIsSafeModeEnabled(isSafeModeEnabled)
    {
    }

    void AutoRepairCommandService::OnHotkey()
    {
        if (!SyncSession())
        {
            return;
        }
        mEnabled = !mEnabled;
        ResetSafeRepairSchedule();
    }

    void AutoRepairCommandService::OnGameFrame()
    {
        if (!SyncSession() || !mEnabled)
        {
            return;
        }

        const bool isSafeModeEnabled = mIsSafeModeEnabled.load(std::memory_order_acquire);
        Snapshot snapshot;
        if (!mGame.CaptureSnapshot(snapshot, isSafeModeEnabled) || snapshot.LocalOwner == 0)
        {
            return;
        }

        const auto nowMs = isSafeModeEnabled ? mGame.GetCurrentTimeMs() : 0;
        if (!isSafeModeEnabled)
        {
            ResetSafeRepairSchedule();
        }

        const auto frame = mLastObservedFrame;
        bool hasSafeRepairCandidate = false;
        for (const auto& building : snapshot.Buildings)
        {
            if (building.Id.Address == 0 || building.Id.UniqueId == 0 ||
                building.Owner != snapshot.LocalOwner ||
                !building.IsDamaged || building.IsBeingRepaired || !building.CanBeRepaired)
            {
                continue;
            }

            if (isSafeModeEnabled)
            {
                if (!building.IsInViewport)
                {
                    continue;
                }
                hasSafeRepairCandidate = true;
                if (!mHasSafeRepairSchedule)
                {
                    mNextSafeRepairAtMs = nowMs + SafeRepairDelayMs(
                        building.Id, frame, building.Health,
                        MIN_FIRST_SAFE_REPAIR_DELAY_MS, MAX_FIRST_SAFE_REPAIR_DELAY_MS);
                    mHasSafeRepairSchedule = true;
                }
                if (nowMs < mNextSafeRepairAtMs)
                {
                    break;
                }
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
            if (mGame.TryRepair(building.Id, isSafeModeEnabled))
            {
                mLastAttemptFrames[building.Id] = frame;
                if (isSafeModeEnabled)
                {
                    mNextSafeRepairAtMs = nowMs + SafeRepairDelayMs(
                        building.Id, frame, building.Health,
                        MIN_NEXT_SAFE_REPAIR_DELAY_MS, MAX_NEXT_SAFE_REPAIR_DELAY_MS);
                    break;
                }
            }
        }
        if (isSafeModeEnabled && !hasSafeRepairCandidate)
        {
            // 无可修建筑时结束本批次；下一次出现候选重新等待首栋延迟。
            ResetSafeRepairSchedule();
        }
    }

    void AutoRepairCommandService::OnSafeModeChanged()
    {
        ResetSafeRepairSchedule();
    }

    void AutoRepairCommandService::Reset()
    {
        mEnabled = false;
        mHasSession = false;
        mSessionIdentity = 0;
        mLastObservedFrame = 0;
        mLastAttemptFrames.clear();
        ResetSafeRepairSchedule();
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

    void AutoRepairCommandService::ResetSafeRepairSchedule()
    {
        mNextSafeRepairAtMs = 0;
        mHasSafeRepairSchedule = false;
    }
}
