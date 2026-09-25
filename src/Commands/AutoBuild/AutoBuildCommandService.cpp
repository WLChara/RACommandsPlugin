#include "Commands/AutoBuild/AutoBuildCommandService.h"

#include <initializer_list>

namespace ra_commands::auto_build
{
    namespace
    {
        constexpr std::uint32_t PRODUCE_COOLDOWN_FRAMES = 15;
        constexpr std::uint32_t START_CONFIRM_TIMEOUT_FRAMES = 120;
    }

    AutoBuildCommandService::AutoBuildCommandService(IAutoBuildGamePort& game)
        : mGame(game)
    {
    }

    bool AutoBuildCommandService::OnHotkey(BuildSlot slot)
    {
        if (!SyncSession())
        {
            return false;
        }

        auto& state = mSlots[Index(slot)];
        if (state.Phase != BuildPhase::Off)
        {
            state = {};
            return false;
        }

        state.Phase = BuildPhase::Armed;
        SlotSnapshot snapshot;
        if (mGame.CaptureSlot(slot, snapshot))
        {
            ProcessSlot(slot, state, snapshot, mLastObservedFrame);
        }
        return true;
    }

    void AutoBuildCommandService::OnGameFrame()
    {
        if (!SyncSession())
        {
            return;
        }

        for (const auto slot : {BuildSlot::Main, BuildSlot::Defense})
        {
            auto& state = mSlots[Index(slot)];
            if (state.Phase == BuildPhase::Off)
            {
                continue;
            }
            SlotSnapshot snapshot;
            if (mGame.CaptureSlot(slot, snapshot))
            {
                ProcessSlot(slot, state, snapshot, mLastObservedFrame);
            }
        }
    }

    void AutoBuildCommandService::Reset()
    {
        mSlots = {};
        mSessionIdentity = 0;
        mLastObservedFrame = 0;
        mHasSession = false;
    }

    BuildPhase AutoBuildCommandService::GetPhase(BuildSlot slot) const noexcept
    {
        return mSlots[Index(slot)].Phase;
    }

    bool AutoBuildCommandService::SyncSession()
    {
        if (!mGame.IsMatchReady())
        {
            Reset();
            return false;
        }

        const auto identity = mGame.GetSessionIdentity();
        if (identity == 0)
        {
            Reset();
            return false;
        }
        const auto frame = mGame.GetCurrentFrame();
        if (!mHasSession || identity != mSessionIdentity ||
            frame < mLastObservedFrame)
        {
            Reset();
            mSessionIdentity = identity;
            mHasSession = true;
        }
        mLastObservedFrame = frame;
        return true;
    }

    void AutoBuildCommandService::ProcessSlot(BuildSlot slot, SlotState& state,
        const SlotSnapshot& snapshot, std::uint32_t frame)
    {
        if (state.Phase == BuildPhase::Armed)
        {
            if (CanTrack(slot, snapshot))
            {
                state.Target = snapshot.Product;
                state.Phase = snapshot.IsReady ? BuildPhase::Ready : BuildPhase::Building;
            }
            return;
        }

        if (!state.Target || snapshot.IsManuallyStopped)
        {
            state = {};
            return;
        }

        const bool sameProduct = snapshot.Product &&
            *snapshot.Product == *state.Target;
        if (state.Phase == BuildPhase::Queued)
        {
            if (sameProduct)
            {
                state.Phase = snapshot.IsReady ? BuildPhase::Ready : BuildPhase::Building;
            }
            else if (snapshot.Product ||
                frame - state.QueuedAtFrame >= START_CONFIRM_TIMEOUT_FRAMES)
            {
                // 已入队的事件无法撤回；未见目标开工时停止，避免重发。
                state = {};
            }
            return;
        }

        if (state.Phase == BuildPhase::Building)
        {
            if (sameProduct)
            {
                if (snapshot.IsReady)
                {
                    state.Phase = BuildPhase::Ready;
                }
            }
            else if (snapshot.Product || snapshot.IsEmpty)
            {
                // 完工前换品种或取消视为玩家接管。
                state = {};
            }
            return;
        }

        if (state.Phase != BuildPhase::Ready)
        {
            return;
        }
        if (sameProduct)
        {
            if (snapshot.IsInProgress && !snapshot.IsReady)
            {
                state.Phase = BuildPhase::Building;
            }
            return;
        }
        if (!snapshot.IsEmpty)
        {
            // 其他品种占用建造栏时保留原任务，等待其离开。
            return;
        }
        if (state.LastEnqueueFrame &&
            frame - *state.LastEnqueueFrame < PRODUCE_COOLDOWN_FRAMES)
        {
            return;
        }
        if (mGame.TryEnqueueProduce(slot, *state.Target))
        {
            state.LastEnqueueFrame = frame;
            state.QueuedAtFrame = frame;
            state.Phase = BuildPhase::Queued;
        }
    }

    bool AutoBuildCommandService::CanTrack(BuildSlot slot,
        const SlotSnapshot& snapshot)
    {
        return snapshot.Product && snapshot.Product->TypeIndex >= 0 &&
            !snapshot.Product->RegisteredName.empty() &&
            (slot == BuildSlot::Defense) == snapshot.IsCombat &&
            !(snapshot.BuildLimit >= 1 && snapshot.BuildLimit <= 100) &&
            !snapshot.IsManuallyStopped &&
            (snapshot.IsReady || snapshot.IsInProgress);
    }

    std::size_t AutoBuildCommandService::Index(BuildSlot slot) noexcept
    {
        return slot == BuildSlot::Main ? 0 : 1;
    }
}
