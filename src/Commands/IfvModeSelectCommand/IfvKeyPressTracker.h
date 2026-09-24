#pragma once

#include <cstdint>

namespace ra_commands::ifv_select
{
    enum class PressPhase
    {
        Ignored,
        FirstPress,
        SecondPress
    };

    /** 利用游戏 WWKey 的释放事件区分独立按键与长按重复；时间窗由上层服务判断。 */
    class IfvKeyPressTracker final
    {
    public:
        [[nodiscard]] PressPhase Consume(std::uint32_t eventKey) noexcept;
        void Reset() noexcept;

    private:
        // 目标样本的键盘分发器 0x55DEE0 用这一位区分释放事件。
        static constexpr std::uint32_t RELEASE_FLAG = 0x800u;
        // 低字节是实体键码；修饰键可能在主键释放前先变化。
        static constexpr std::uint32_t KEY_CODE_MASK = 0xFFu;

        std::uint32_t mKeyCode = 0;
        std::uint32_t mBindingKey = 0;
        bool mIsHeld = false;
        bool mHasReleased = false;
    };
}
