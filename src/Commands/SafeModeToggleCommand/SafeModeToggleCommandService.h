#pragma once

#include <atomic>
#include <cstdint>

namespace ra_commands::safe_mode
{
    /** 只在游戏主线程切换安全模式，并在对局代次改变时重置状态。 */
    class SafeModeToggleCommandService final
    {
    public:
        explicit SafeModeToggleCommandService(std::atomic<bool>& isEnabled);

        [[nodiscard]] bool OnHotkey(bool isMatchActive) noexcept;
        void OnGameFrame(bool isMatchActive, std::uint32_t epoch) noexcept;
        void Reset() noexcept;

    private:
        std::atomic<bool>& mIsEnabled;
        std::uint32_t mEpoch = 0;
    };
}
