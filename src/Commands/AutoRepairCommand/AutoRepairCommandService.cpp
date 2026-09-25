#include "Commands/AutoRepairCommand/AutoRepairCommandService.h"

#include <unordered_set>

namespace ra_commands::auto_repair
{
    namespace
    {
        // 原生修理状态可能晚于事件发送回写，30 帧内不重发同一建筑。
        constexpr std::uint32_t RETRY_INTERVAL_FRAMES = 30;
        constexpr std::uint32_t MIN_SAFE_REPAIR_DELAY_MS = 1'000;
        constexpr std::uint32_t MAX_SAFE_REPAIR_DELAY_MS = 4'000;

        std::uint32_t SafeRepairDelayMs(BuildingId id, std::uint32_t frame, int health)
        {
            // 使用稳定的建筑身份和受伤帧选择延迟，避免每帧重新抽取时间。
            std::uint32_t value = id.UniqueId * 0x9E3779B9u ^
                frame * 0x85EBCA6Bu ^ static_cast<std::uint32_t>(health);
            value ^= value >> 16;
            value *= 0x7FEB352Du;
            value ^= value >> 15;
            return MIN_SAFE_REPAIR_DELAY_MS +
                value % (MAX_SAFE_REPAIR_DELAY_MS - MIN_SAFE_REPAIR_DELAY_MS + 1);
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
    }

    void AutoRepairCommandService::OnGameFrame()
    {
        if (!SyncSession())
        {
            return;
        }

        const bool isSafeModeEnabled = mIsSafeModeEnabled.load(std::memory_order_acquire);
        if (!mEnabled && !isSafeModeEnabled)
        {
            return;
        }

        Snapshot snapshot;
        if (!mGame.CaptureSnapshot(snapshot, isSafeModeEnabled) || snapshot.LocalOwner == 0)
        {
            return;
        }

        const auto nowMs = isSafeModeEnabled ? mGame.GetCurrentTimeMs() : 0;
        if (isSafeModeEnabled)
        {
            TrackDamage(snapshot, nowMs);
        }
        else
        {
            mDamageStates.clear();
        }
        if (!mEnabled)
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

            if (isSafeModeEnabled)
            {
                const auto damage = mDamageStates.find(building.Id);
                if (!building.IsInViewport || damage == mDamageStates.end() ||
                    damage->second.ReadyAtMs == 0 || nowMs < damage->second.ReadyAtMs)
                {
                    continue;
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
                    break;
                }
            }
        }
    }

    void AutoRepairCommandService::OnSafeModeChanged()
    {
        mDamageStates.clear();
    }

    void AutoRepairCommandService::Reset()
    {
        mEnabled = false;
        mHasSession = false;
        mSessionIdentity = 0;
        mLastObservedFrame = 0;
        mLastAttemptFrames.clear();
        mDamageStates.clear();
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

    void AutoRepairCommandService::TrackDamage(const Snapshot& snapshot, std::uint64_t nowMs)
    {
        std::unordered_set<BuildingId, BuildingIdHash> seen;
        for (const auto& building : snapshot.Buildings)
        {
            if (building.Owner != snapshot.LocalOwner || building.Id.Address == 0 ||
                building.Id.UniqueId == 0 || building.Health <= 0)
            {
                continue;
            }
            seen.insert(building.Id);
            const auto [it, inserted] = mDamageStates.try_emplace(building.Id);
            auto& state = it->second;
            if (building.IsDamaged && (inserted || building.Health < state.LastHealth))
            {
                state.ReadyAtMs = nowMs +
                    SafeRepairDelayMs(building.Id, mLastObservedFrame, building.Health);
            }
            else if (!building.IsDamaged)
            {
                state.ReadyAtMs = 0;
            }
            state.LastHealth = building.Health;
        }
        std::erase_if(mDamageStates, [&seen](const auto& item)
        {
            return !seen.contains(item.first);
        });
    }
}
