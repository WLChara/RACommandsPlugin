#pragma once

#include <cstdint>

namespace ra_commands::game
{
    struct PluginOrderContext
    {
        std::uint8_t mProducerTag = 0xFF;
        std::uint32_t mEpoch = 0;
    };
    /**
     * 标记当前线程正在发出插件意图，避免同步原生 Hook 将其误判为玩家新命令。
     * 只能围住实际下令调用；作用域退出（含异常）后恢复外层状态。
     */
    class PluginOrderScope final
    {
    public:
        explicit PluginOrderScope(PluginOrderContext context = {}) noexcept;
        ~PluginOrderScope();
        PluginOrderScope(const PluginOrderScope&) = delete;
        PluginOrderScope& operator=(const PluginOrderScope&) = delete;

    private:
        PluginOrderContext mPrevious;
    };

    [[nodiscard]] bool IsPluginOrderActive() noexcept;
    // 返回值只标记本次同步下令的来源；退出 scope 后恢复外层，不保存游戏指针。
    [[nodiscard]] PluginOrderContext GetPluginOrderContext() noexcept;
}
