#pragma once

namespace ra_commands::game
{
    /**
     * 标记当前线程正在发出插件意图，避免同步原生 Hook 将其误判为玩家新命令。
     * 只能围住实际下令调用；作用域退出（含异常）后恢复外层状态。
     */
    class PluginOrderScope final
    {
    public:
        PluginOrderScope() noexcept;
        ~PluginOrderScope();
        PluginOrderScope(const PluginOrderScope&) = delete;
        PluginOrderScope& operator=(const PluginOrderScope&) = delete;
    };

    [[nodiscard]] bool IsPluginOrderActive() noexcept;
}
