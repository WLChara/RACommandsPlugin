#include "Commands/AutoSuperWeapon/AutoSuperWeaponCommandService.h"

namespace ra_commands::auto_super_weapon
{
    namespace
    {
        constexpr std::uint32_t RETRY_INTERVAL_FRAMES = 30;
    }

    AutoSuperWeaponCommandService::AutoSuperWeaponCommandService(
        IAutoSuperWeaponGamePort& game) : mGame(game)
    {
    }

    bool AutoSuperWeaponCommandService::OnHotkey(Kind kind)
    {
        if (!SyncSession())
        {
            return false;
        }
        auto& state = mStates[Index(kind)];
        if (state.Enabled)
        {
            state = {};
            return false;
        }
        state.Enabled = true;
        Process(kind, state);
        return true;
    }

    void AutoSuperWeaponCommandService::OnGameFrame()
    {
        if (!SyncSession())
        {
            return;
        }
        Process(Kind::IronCurtain, mStates[Index(Kind::IronCurtain)]);
        Process(Kind::RageInductor, mStates[Index(Kind::RageInductor)]);
    }

    void AutoSuperWeaponCommandService::Reset() noexcept
    {
        mStates = {};
        mSessionIdentity = 0;
        mLastFrame = 0;
        mHasSession = false;
    }

    bool AutoSuperWeaponCommandService::IsEnabled(Kind kind) const noexcept
    {
        return mStates[Index(kind)].Enabled;
    }

    bool AutoSuperWeaponCommandService::SyncSession()
    {
        if (!mGame.IsMatchReady())
        {
            Reset();
            return false;
        }
        const auto identity = mGame.SessionIdentity();
        if (identity == 0)
        {
            Reset();
            return false;
        }
        const auto frame = mGame.CurrentFrame();
        if (!mHasSession || identity != mSessionIdentity || frame < mLastFrame)
        {
            Reset();
            mSessionIdentity = identity;
            mHasSession = true;
        }
        mLastFrame = frame;
        return true;
    }

    void AutoSuperWeaponCommandService::Process(Kind kind, State& state)
    {
        if (!state.Enabled)
        {
            return;
        }
        if (!mGame.IsWeaponReady(kind))
        {
            state.Pending = false;
            return;
        }
        if (state.Pending)
        {
            // 入队不等于执行；未见就绪状态消失前不能盲目重发。
            return;
        }
        if (state.LastAttemptFrame &&
            mLastFrame - *state.LastAttemptFrame < RETRY_INTERVAL_FRAMES)
        {
            return;
        }
        state.LastAttemptFrame = mLastFrame;
        Snapshot snapshot;
        if (!mGame.CaptureSnapshot(kind, snapshot))
        {
            return;
        }
        const auto plan = Plan(kind, snapshot);
        if (plan && mGame.TryFireAt(kind, plan->Center))
        {
            state.Pending = true;
        }
    }

    std::size_t AutoSuperWeaponCommandService::Index(Kind kind) noexcept
    {
        return kind == Kind::IronCurtain ? 0 : 1;
    }
}
