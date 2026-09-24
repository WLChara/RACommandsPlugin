#pragma once

#include "Commands/AFloorCommand/IAFloorGamePort.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace ra_commands::a_floor
{
    /**
     * 拥有本地输入模式的开关与对局边界；游戏线程调用热键和逐帧入口。
     * Reset 可由外部 Shutdown 调用，IsEnabled 可由 Hook 并发读取。
     * 端口对象由调用方持有，必须比服务存活更久。
     */
    class AFloorCommandService final
    {
    public:
        explicit AFloorCommandService(IAFloorGamePort& game);

        void OnHotkey();
        void OnGameFrame();
        void Reset();
        [[nodiscard]] bool IsEnabled() const noexcept;

    private:
        [[nodiscard]] bool SyncSession();
        void ResetState() noexcept;

        IAFloorGamePort& mGame;
        std::mutex mMutex;
        std::uintptr_t mSessionIdentity = 0;
        std::uint32_t mLastObservedFrame = 0;
        bool mHasSession = false;
        std::atomic<bool> mIsEnabled{false};
    };
}
