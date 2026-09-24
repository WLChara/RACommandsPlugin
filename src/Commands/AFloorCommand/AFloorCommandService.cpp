#include "Commands/AFloorCommand/AFloorCommandService.h"

namespace ra_commands::a_floor
{
    AFloorCommandService::AFloorCommandService(IAFloorGamePort& game)
        : mGame(game)
    {
    }

    void AFloorCommandService::OnHotkey()
    {
        std::lock_guard lock(mMutex);
        if (!SyncSession())
        {
            return;
        }
        mIsEnabled.store(!mIsEnabled.load(std::memory_order_relaxed),
            std::memory_order_release);
    }

    void AFloorCommandService::OnGameFrame()
    {
        std::lock_guard lock(mMutex);
        (void)SyncSession();
    }

    void AFloorCommandService::Reset()
    {
        std::lock_guard lock(mMutex);
        ResetState();
    }

    bool AFloorCommandService::IsEnabled() const noexcept
    {
        return mIsEnabled.load(std::memory_order_acquire);
    }

    bool AFloorCommandService::SyncSession()
    {
        if (!mGame.IsMatchReady())
        {
            ResetState();
            return false;
        }

        const auto sessionIdentity = mGame.GetSessionIdentity();
        if (sessionIdentity == 0)
        {
            ResetState();
            return false;
        }

        const auto frame = mGame.GetCurrentFrame();
        if (!mHasSession || mSessionIdentity != sessionIdentity ||
            frame < mLastObservedFrame)
        {
            ResetState();
            mHasSession = true;
            mSessionIdentity = sessionIdentity;
        }
        mLastObservedFrame = frame;
        return true;
    }

    void AFloorCommandService::ResetState() noexcept
    {
        // 对外可读的状态先关闭，避免 Hook 在重置期间看到旧模式。
        mIsEnabled.store(false, std::memory_order_release);
        mHasSession = false;
        mSessionIdentity = 0;
        mLastObservedFrame = 0;
    }
}
