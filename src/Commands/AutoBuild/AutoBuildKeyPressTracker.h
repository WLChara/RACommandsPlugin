#pragma once

#include <cstdint>

namespace ra_commands::auto_build
{
    /** 接收原生 WWKey 的按下和释放；一次长按只触发一次切换。 */
    class AutoBuildKeyPressTracker
    {
    public:
        [[nodiscard]] bool Consume(std::uint32_t eventKey) noexcept
        {
            constexpr std::uint32_t RELEASE_FLAG = 0x800u;
            constexpr std::uint32_t KEY_CODE_MASK = 0xFFu;
            const auto keyCode = eventKey & KEY_CODE_MASK;
            if (eventKey & RELEASE_FLAG)
            {
                if (mIsHeld && keyCode == mKeyCode)
                {
                    mIsHeld = false;
                }
                return false;
            }
            if (mIsHeld && keyCode == mKeyCode)
            {
                return false;
            }
            mKeyCode = keyCode;
            mIsHeld = true;
            return true;
        }

        void Reset() noexcept
        {
            mKeyCode = 0;
            mIsHeld = false;
        }

    private:
        std::uint32_t mKeyCode = 0;
        bool mIsHeld = false;
    };
}
