#pragma once

#include "Commands/RangeDisplayCommand/IRangeDisplayGamePort.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace ra_commands::range_display
{
    /**
     * 管理射程显示开关及其对局生命周期；状态可供未来的 Hook 并发读取。
     * 游戏端口由调用方持有，生命周期必须覆盖本服务。
     */
    class RangeDisplayCommandService final
    {
    public:
        explicit RangeDisplayCommandService(IRangeDisplayGamePort& game);

        void OnHotkey();
        void OnGameFrame();
        void Reset();
        [[nodiscard]] bool IsEnabled() const noexcept;

    private:
        [[nodiscard]] bool SyncSession();
        void ResetState() noexcept;

        IRangeDisplayGamePort& mGame;
        std::mutex mMutex;
        std::uintptr_t mSessionIdentity = 0;
        std::uint32_t mLastObservedFrame = 0;
        bool mHasSession = false;
        std::atomic<bool> mIsEnabled{false};
    };
}
