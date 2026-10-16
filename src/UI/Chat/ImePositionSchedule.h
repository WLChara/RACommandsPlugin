#pragma once

#include <cstdint>

namespace ra_commands::ui::chat
{
    /** IMM 定位成功后按窗口/坐标缓存；失败每 250ms 重试，外部生命周期事件可立即失效。 */
    class ImePositionSchedule
    {
    public:
        bool ShouldAttempt(std::uintptr_t window, int x, int y, std::uint64_t nowMs, bool force = false) noexcept
        {
            if (!window) return false;
            constexpr std::uint64_t RETRY_MS = 250;
            const bool rolledBack = mHasAttempt && nowMs < mLastObservedMs;
            mLastObservedMs = nowMs;
            const bool samePosition = mHasAttempt && window == mWindow && x == mX && y == mY;
            if (!force && !rolledBack && samePosition && (mSucceeded || nowMs - mLastAttemptMs < RETRY_MS)) return false;
            mHasAttempt = true;
            mSucceeded = false;
            mWindow = window;
            mX = x;
            mY = y;
            mLastAttemptMs = nowMs;
            return true;
        }
        void CompleteAttempt(bool succeeded) noexcept { mSucceeded = mHasAttempt && succeeded; }
        void Invalidate() noexcept { mHasAttempt = false; mSucceeded = false; }

    private:
        bool mHasAttempt = false;
        bool mSucceeded = false;
        std::uintptr_t mWindow = 0;
        int mX = 0;
        int mY = 0;
        std::uint64_t mLastAttemptMs = 0;
        std::uint64_t mLastObservedMs = 0;
    };
}
