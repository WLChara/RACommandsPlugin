#include "Commands/RangeDisplayCommand/RangeDisplayCommandService.h"

namespace ra_commands::range_display
{
    RangeDisplayCommandService::RangeDisplayCommandService(IRangeDisplayGamePort& game)
        : mGame(game)
    {
    }

    void RangeDisplayCommandService::OnHotkey()
    {
        std::lock_guard lock(mMutex);
        if (!SyncSession())
        {
            return;
        }

        const bool isEnabled = mIsEnabled.load(std::memory_order_relaxed);
        mIsEnabled.store(!isEnabled, std::memory_order_release);
    }

    void RangeDisplayCommandService::OnGameFrame()
    {
        std::lock_guard lock(mMutex);
        (void)SyncSession();
    }

    void RangeDisplayCommandService::Reset()
    {
        std::lock_guard lock(mMutex);
        ResetState();
    }

    bool RangeDisplayCommandService::IsEnabled() const noexcept
    {
        return mIsEnabled.load(std::memory_order_acquire);
    }

    bool RangeDisplayCommandService::SyncSession()
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

    void RangeDisplayCommandService::ResetState() noexcept
    {
        mIsEnabled.store(false, std::memory_order_release);
        mHasSession = false;
        mSessionIdentity = 0;
        mLastObservedFrame = 0;
    }
}
