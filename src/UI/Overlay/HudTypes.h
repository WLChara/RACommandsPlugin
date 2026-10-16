#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ra_commands::ui
{
    struct HudColor
    {
        std::uint8_t mRed = 255;
        std::uint8_t mGreen = 255;
        std::uint8_t mBlue = 255;
    };

    struct HudWorldPoint
    {
        std::int32_t mX = 0;
        std::int32_t mY = 0;
        std::int32_t mZ = 0;
    };

    struct HudMapCell
    {
        std::int16_t mX = 0;
        std::int16_t mY = 0;
    };

    struct ChatMessage
    {
        std::uint64_t mId = 0;
        std::wstring mSender;
        std::wstring mText;
        HudColor mColor;
        std::uint64_t mReceivedAtMs = 0;
        int mPlayerId = -1;
        bool mIsLocal = false;
    };

    /** 值快照不引用原生标签或编辑缓冲区；时间统一为单调时钟毫秒。 */
    struct ChatView
    {
        bool mEnabled = false;
        bool mEditing = false;
        std::wstring mDraft;
        std::size_t mCaret = 0;
        std::size_t mSelectionAnchor = 0;
        std::wstring mComposition;
        std::wstring mChannelLabel;
        std::wstring mStatus;
        std::vector<ChatMessage> mMessages;
    };

    struct BeaconNotice
    {
        std::uint64_t mId = 0;
        int mOwner = -1;
        int mSlot = -1;
        HudWorldPoint mPosition;
        HudColor mColor;
        std::uint64_t mExpiresAtMs = 0;
    };

    struct RadarNotice
    {
        std::uint64_t mId = 0;
        int mType = 0;
        HudMapCell mCell;
        std::uint64_t mReceivedAtMs = 0;
        unsigned int mCount = 1;
        bool mPinned = false;
        bool mRead = false;
        int mPriority = 0;
        std::wstring mLabel;
    };
}
