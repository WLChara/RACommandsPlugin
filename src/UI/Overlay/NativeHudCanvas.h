#pragma once
#include "UI/Overlay/HudGeometry.h"
#include "UI/Overlay/HudTypes.h"
#include "UI/Overlay/HudPixelFormat.h"
#include <cstdint>
#include <vector>

namespace ra_commands::ui
{
    /** 私有原生适配器。只在游戏窗口线程调用，跨帧仅保留像素和表面身份值。 */
    class NativeHudCanvas
    {
    public:
        bool Validate();
        bool Restore();
        bool Begin(HudRect& outBattlefield, int& outWidth, int& outHeight);
        /** 同帧全部区域必须在任何 Draw 前一次性备份，禁止边画边补采背景。 */
        bool PrepareDamage(const std::vector<HudRect>& regions);
        std::uintptr_t GetFontKey() const noexcept;
        int Measure(const std::wstring& text) const;
        int GetLineHeight() const;
        void Black(HudRect rect);
        void Tint(HudRect rect, HudColor color, unsigned int opacity256);
        void Text(const std::wstring& text, HudRect clip, HudPoint point, HudColor color);
        void Line(HudPoint first, HudPoint second, HudColor color, HudRect clip);
        void Fade(HudRect rect, float opacity);
        bool HasPendingRestore() const noexcept;
    private:
        HudRect mBounds;
        int mWidth = 0;
        int mHeight = 0;
        std::uintptr_t mSurfaceIdentity = 0;
        std::uintptr_t mResourceIdentity = 0;
        std::uint16_t mRed = 0;
        std::uint16_t mGreen = 0;
        std::uint16_t mBlue = 0;
        std::vector<std::uint16_t> mBackground;
        std::vector<HudRect> mDamage;
        HudPixelFormat mFormat;
        bool mPrepared = false;
        void AddDamage(HudRect rect);
        std::uint16_t PackColor(HudColor color) const noexcept;
    };
}
