#include "Commands/IfvModeSelectCommand/IfvKeyPressTracker.h"

namespace ra_commands::ifv_select
{
    PressPhase IfvKeyPressTracker::Consume(std::uint32_t eventKey) noexcept
    {
        const auto keyCode = eventKey & KEY_CODE_MASK;
        const auto bindingKey = eventKey & ~RELEASE_FLAG;
        if (eventKey & RELEASE_FLAG)
        {
            if (mIsHeld && keyCode == mKeyCode)
            {
                mIsHeld = false;
                mHasReleased = true;
            }
            return PressPhase::Ignored;
        }

        if (mIsHeld && keyCode == mKeyCode)
        {
            return PressPhase::Ignored;
        }

        const bool isSecondPress = mHasReleased && bindingKey == mBindingKey;
        mKeyCode = keyCode;
        mBindingKey = bindingKey;
        mIsHeld = true;
        mHasReleased = false;
        return isSecondPress ? PressPhase::SecondPress : PressPhase::FirstPress;
    }

    void IfvKeyPressTracker::Reset() noexcept
    {
        mKeyCode = 0;
        mBindingKey = 0;
        mIsHeld = false;
        mHasReleased = false;
    }
}
