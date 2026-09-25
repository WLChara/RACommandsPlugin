#pragma once

#include <cstdint>
#include <optional>

namespace ra_commands::game
{
    class NativeNetworkEventAdapter;

    /** 在游戏线程读取鼠标格，并提交本地玩家的力场护盾事件。 */
    class ForceShieldGameAdapter
    {
    public:
        explicit ForceShieldGameAdapter(NativeNetworkEventAdapter& events);

        [[nodiscard]] bool TryFireAtCursor();
        void Reset() noexcept;

    private:
        NativeNetworkEventAdapter& mEvents;
        std::uintptr_t mLastHouse = 0;
        std::uint32_t mLastSuperIndex = 0;
        std::optional<std::uint32_t> mLastQueuedFrame;
    };
}
