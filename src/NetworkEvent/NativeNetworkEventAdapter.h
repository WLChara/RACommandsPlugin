#pragma once

#include "NetworkEvent/NetworkEvent.h"

#include <atomic>
#include <cstdint>

namespace ra_commands::game
{
    /** 目标 EXE 的原生 OutList 适配器；由 Bootstrap 绑定并仅在游戏线程调用。 */
    class NativeNetworkEventAdapter
    {
    public:
        void BindGameThread(std::uint32_t threadId) noexcept;
        void Reset() noexcept;

        /** true 表示事件已写入原生 OutList，不表示游戏已执行该事件。 */
        [[nodiscard]] bool TryEnqueueLocal(
            const network_event::NetworkEvent& event) const;

    private:
        std::atomic<std::uint32_t> mGameThreadId{0};
    };
}
