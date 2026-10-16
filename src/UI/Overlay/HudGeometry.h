#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ra_commands::ui
{
    struct HudPoint { int mX = 0; int mY = 0; };
    struct HudRect { int mX = 0; int mY = 0; int mWidth = 0; int mHeight = 0; };
    struct HudLayout
    {
        HudRect mBattlefield;
        HudRect mChat;
        HudRect mInput;
        HudRect mFeed;
    };
    struct HudArrow { HudPoint mTip; HudPoint mLeft; HudPoint mRight; };
    enum class HudAction { None, RadarJump, RadarPin, ChatCopy, ChatCaret, ChatScroll, FeedScroll };
    struct HudHit
    {
        HudRect mBounds;
        HudAction mAction = HudAction::None;
        std::uint64_t mId = 0;
    };
    /** 命中只使用已成功绘制的那一帧；按下后即使布局改变也成对消费抬起。 */
    class HudPointerOwnership
    {
    public:
        bool Begin(unsigned int button, bool isHit) noexcept;
        bool End(unsigned int button) noexcept;
        void Cancel() noexcept;
        unsigned int GetButtons() const noexcept;
    private:
        unsigned int mButtons = 0;
    };
    class HudRenderHealth
    {
    public:
        bool ObserveFailure(std::uint64_t nowMs, bool isVisible) noexcept;
        void ObserveSuccess() noexcept;
    private:
        std::uint64_t mFailureAtMs = 0;
        bool mHasVisibleFailure = false;
    };
    bool Contains(HudRect rect, HudPoint point) noexcept;
    HudRect Intersect(HudRect first, HudRect second) noexcept;
    /** hasFeed 表示功能已开启；空列表和聊天展开均保留侧栏，必要时缩短聊天历史。 */
    HudLayout CalculateHudLayout(HudRect battlefield, bool editing, bool hasChat, bool hasFeed);
    float ChatOpacity(std::uint64_t receivedAtMs, std::uint64_t nowMs) noexcept;
    HudPoint ClientToSurface(HudPoint point, int clientWidth, int clientHeight,
        int surfaceWidth, int surfaceHeight) noexcept;
    HudRect CalculateNativeHudCrop(int width, int height, double zoom) noexcept;
    HudPoint ClientToHudSurface(HudPoint point, int clientWidth, int clientHeight,
        int windowWidth, int windowHeight, HudRect source, HudRect destination) noexcept;
    HudPoint HudSurfaceToClient(HudPoint point, int clientWidth, int clientHeight,
        int windowWidth, int windowHeight, HudRect source, HudRect destination) noexcept;
    bool TryPlaceEdgeArrow(HudRect viewport, HudPoint target,
        const std::vector<HudArrow>& occupied, HudArrow& outArrow);
    using HudMeasure = std::function<int(const std::wstring&)>;
    /** 最后一行预留复制图标；必要时增加空末行，绝不让图标覆盖文字。 */
    std::vector<std::wstring> WrapHudText(const std::wstring& text,
        int width, int lastLineReserve, const HudMeasure& measure);
    bool IsSupportedPixelMasks(std::uint16_t red, std::uint16_t green,
        std::uint16_t blue) noexcept;
    std::uint16_t BlendHudPixel(std::uint16_t background, std::uint16_t foreground,
        unsigned int opacity256, std::uint16_t red, std::uint16_t green,
        std::uint16_t blue) noexcept;
    bool CaptureHudBackground(const std::uint16_t* pixels, int pitchPixels,
        HudRect bounds, std::vector<std::uint16_t>& outBackground);
    bool RestoreHudBackground(std::uint16_t* pixels, int pitchPixels, HudRect bounds,
        const std::vector<std::uint16_t>& background, const std::vector<HudRect>& damage);
    bool CaptureHudDamageBackground(const std::uint16_t* pixels, int pitchPixels, HudRect bounds,
        const std::vector<HudRect>& damage, std::vector<std::uint16_t>& outBackground);
    bool IsHudRectCovered(HudRect rect, const std::vector<HudRect>& prepared);
    struct HudSurfaceIdentity
    {
        std::uintptr_t mSurface = 0, mResource = 0;
        int mWidth = 0, mHeight = 0;
        std::uint16_t mRed = 0, mGreen = 0, mBlue = 0;
        bool operator==(const HudSurfaceIdentity&) const = default;
    };
    bool CanRestoreHudSurface(const HudSurfaceIdentity& saved, const HudSurfaceIdentity& current, bool wasLost) noexcept;
}
