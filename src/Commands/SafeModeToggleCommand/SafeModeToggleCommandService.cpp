#include "Commands/SafeModeToggleCommand/SafeModeToggleCommandService.h"

namespace ra_commands::safe_mode
{
    SafeModeToggleCommandService::SafeModeToggleCommandService(std::atomic<bool>& isEnabled)
        : mIsEnabled(isEnabled)
    {
    }

    bool SafeModeToggleCommandService::OnHotkey(bool isMatchActive) noexcept
    {
        if (!isMatchActive)
        {
            return false;
        }

        const bool isEnabled = !mIsEnabled.load(std::memory_order_acquire);
        mIsEnabled.store(isEnabled, std::memory_order_release);
        return isEnabled;
    }

    void SafeModeToggleCommandService::OnGameFrame(bool isMatchActive, std::uint32_t epoch) noexcept
    {
        if (!isMatchActive)
        {
            Reset();
            return;
        }

        if (mEpoch != epoch)
        {
            Reset();
            mEpoch = epoch;
        }
    }

    void SafeModeToggleCommandService::Reset() noexcept
    {
        mIsEnabled.store(false, std::memory_order_release);
        mEpoch = 0;
    }
}
