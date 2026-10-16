#pragma once

#include <cstdint>

namespace ra_commands::ui
{
    /** 信标删除及关系变化最多延迟 100ms 校对；放置信标事件不经过此调度。 */
    class BeaconReconcileSchedule
    {
    public:
        void Reset() noexcept { mHasReconciled = false; }
        bool ShouldReconcile(std::uint64_t nowMs) noexcept
        {
            constexpr std::uint64_t INTERVAL_MS = 100;
            const bool rolledBack = mHasReconciled && nowMs < mLastObservedMs;
            mLastObservedMs = nowMs;
            if (mHasReconciled && !rolledBack && nowMs - mLastReconcileMs < INTERVAL_MS) return false;
            mHasReconciled = true;
            mLastReconcileMs = nowMs;
            return true;
        }

    private:
        bool mHasReconciled = false;
        std::uint64_t mLastReconcileMs = 0;
        std::uint64_t mLastObservedMs = 0;
    };
}
