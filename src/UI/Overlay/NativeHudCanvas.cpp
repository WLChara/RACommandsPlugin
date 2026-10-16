#include "UI/Overlay/NativeHudCanvas.h"
#include "UI/Overlay/HudTextAbi.h"
#include "Game/TargetProfiles/HudUiProfile.h"
#include "Memory/ProcessMemory.h"
#include <YRPPCore.h>
#include <Surface.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ra_commands::ui
{
    namespace
    {
        template<std::size_t TSize>
        bool MatchesTextEntry(std::uintptr_t address, const std::array<std::uint8_t, TSize>& expected)
        {
            std::array<std::uint8_t, TSize> actual{};
            return memory::TryReadMemory(address, actual.data(), actual.size()) && actual == expected;
        }
        class SurfaceLock final
        {
        public:
            explicit SurfaceLock(DSurface* surface) : mSurface(surface)
            {
                if (!surface || surface->Width <= 0 || surface->Height <= 0 ||
                    surface->Width > 8192 || surface->Height > 8192 || surface->LockCount != 0 ||
                    !surface->Surface || surface->GetBytesPerPixel() != 2) { return; }
                // IDirectDrawSurface::IsLost 为 COM vtable +0x60；4BAD80 在 Lock 前使用同一路径。
                using IsLostFunction = HRESULT(__stdcall*)(IDirectDrawSurface*);
                const auto table = *reinterpret_cast<void***>(surface->Surface);
                mWasLost = reinterpret_cast<IsLostFunction>(table[24])(surface->Surface) != S_OK;
                mPixels = static_cast<std::uint8_t*>(surface->Lock(0, 0));
                if (!mPixels) { return; }
                mPitch = surface->GetPitch();
                const auto* description = surface->SurfaceDesc;
                // 原生 4BA5A0 分配 DDSURFACEDESC v1 的 108 字节，SDK 把指针标为 v2。
                // 仅访问二者共用的像素格式和首个 caps DWORD，禁止按 v2 读取尾部。
                if (!description || description->dwSize != 108 ||
                    description->ddpfPixelFormat.dwRGBBitCount != 16 ||
                    !(description->ddpfPixelFormat.dwFlags & 0x40u) ||
                    (description->ddsCaps.dwCaps & 0x200u) ||
                    mPitch < surface->Width * 2 || mPitch > 32768 || mPitch % 2 != 0)
                { return; }
                const auto& format = description->ddpfPixelFormat;
                if ((format.dwRBitMask | format.dwGBitMask | format.dwBBitMask) > 0xFFFFu) { return; }
                mRed = static_cast<std::uint16_t>(format.dwRBitMask);
                mGreen = static_cast<std::uint16_t>(format.dwGBitMask);
                mBlue = static_cast<std::uint16_t>(format.dwBBitMask);
                mIsValid = IsSupportedPixelMasks(mRed, mGreen, mBlue);
            }
            ~SurfaceLock() { if (mPixels) { (void)mSurface->Unlock(); } }
            bool IsValid() const noexcept { return mIsValid; }
            std::uint16_t* Row(int y) const { return reinterpret_cast<std::uint16_t*>(mPixels + y * mPitch); }
            int GetPitchPixels() const noexcept { return mPitch / 2; }
            bool WasLost() const noexcept { return mWasLost; }
            std::uint16_t GetRed() const noexcept { return mRed; }
            std::uint16_t GetGreen() const noexcept { return mGreen; }
            std::uint16_t GetBlue() const noexcept { return mBlue; }
        private:
            DSurface* mSurface = nullptr;
            std::uint8_t* mPixels = nullptr;
            int mPitch = 0;
            std::uint16_t mRed = 0;
            std::uint16_t mGreen = 0;
            std::uint16_t mBlue = 0;
            bool mIsValid = false;
            bool mWasLost = false;
        };
        RectangleStruct NativeRect(HudRect rect) { return {rect.mX, rect.mY, rect.mWidth, rect.mHeight}; }
    }
    bool NativeHudCanvas::Validate()
    {
        using namespace game::hud_profile;
        if (!MatchesTextEntry(GET_TEXT_DIMENSIONS_ADDRESS, GET_TEXT_DIMENSIONS_ENTRY) ||
            !MatchesTextEntry(GET_TEXT_DIMENSIONS_RETURN_ADDRESS, GET_TEXT_DIMENSIONS_RETURN_ENTRY) ||
            !MatchesTextEntry(DRAW_TEXT_ADDRESS, DRAW_TEXT_ENTRY) ||
            !MatchesTextEntry(DRAW_TEXT_RETURN_ADDRESS, DRAW_TEXT_RETURN_ENTRY)) { return false; }
        SurfaceLock locked(DSurface::Composite.get());
        return locked.IsValid();
    }
    bool NativeHudCanvas::HasPendingRestore() const noexcept { return !mDamage.empty(); }
    bool NativeHudCanvas::Restore()
    {
        if (mDamage.empty()) { return true; }
        auto* const surface = DSurface::Composite.get();
        if (!surface) { return false; }
        if (reinterpret_cast<std::uintptr_t>(surface) != mSurfaceIdentity ||
            surface->Width != mWidth || surface->Height != mHeight)
        {
            // 分辨率/表面已更换：旧备份不允许写入新的渲染资源。
            mDamage.clear();
            return true;
        }
        SurfaceLock locked(surface);
        if (!locked.IsValid()) { return false; }
        if (!CanRestoreHudSurface({mSurfaceIdentity, mResourceIdentity, mWidth, mHeight, mRed, mGreen, mBlue},
            {reinterpret_cast<std::uintptr_t>(surface), reinterpret_cast<std::uintptr_t>(surface->Surface),
                surface->Width, surface->Height, locked.GetRed(), locked.GetGreen(), locked.GetBlue()}, locked.WasLost()))
        {
            mDamage.clear();
            return true;
        }
        if (!RestoreHudBackground(locked.Row(0), locked.GetPitchPixels(), mBounds, mBackground, mDamage)) { return false; }
        mDamage.clear();
        return true;
    }
    bool NativeHudCanvas::Begin(HudRect& outBattlefield, int& outWidth, int& outHeight)
    {
        if (!mDamage.empty()) { return false; }
        auto* const surface = DSurface::Composite.get();
        SurfaceLock locked(surface);
        if (!locked.IsValid()) { return false; }
        const auto view = DSurface::ViewBounds.get();
        const auto sidebar = DSurface::SidebarBounds.get();
        HudRect bounds = Intersect({view.X, view.Y, view.Width, view.Height},
            {0, 0, surface->Width, surface->Height});
        if (sidebar.Width > 0 && sidebar.X > bounds.mX && sidebar.X < bounds.mX + bounds.mWidth)
        { bounds.mWidth = sidebar.X - bounds.mX; }
        if (bounds.mWidth <= 0 || bounds.mHeight <= 0) { return false; }
        mWidth = surface->Width;
        mHeight = surface->Height;
        mBounds = bounds;
        mSurfaceIdentity = reinterpret_cast<std::uintptr_t>(surface);
        // Lock 返回地址可在正常重映射时变化，不能把它当资源代次或跨帧缓存指针。
        mResourceIdentity = reinterpret_cast<std::uintptr_t>(surface->Surface);
        mRed = locked.GetRed();
        mGreen = locked.GetGreen();
        mBlue = locked.GetBlue();
        if (!mFormat.Configure(mRed, mGreen, mBlue)) return false;
        mBackground.resize(static_cast<std::size_t>(bounds.mWidth) * bounds.mHeight);
        mPrepared = false;
        outBattlefield = bounds;
        outWidth = mWidth;
        outHeight = mHeight;
        return true;
    }
    bool NativeHudCanvas::PrepareDamage(const std::vector<HudRect>& regions)
    {
        if (mPrepared || !mDamage.empty()) return false;
        std::vector<HudRect> clipped;
        for (const auto region : regions)
        {
            const auto rect = Intersect(region, mBounds);
            if (rect.mWidth > 0 && rect.mHeight > 0 && !IsHudRectCovered(rect, clipped)) clipped.push_back(rect);
        }
        if (clipped.empty()) { mPrepared = true; return true; }
        auto* const surface = DSurface::Composite.get();
        if (!surface || reinterpret_cast<std::uintptr_t>(surface) != mSurfaceIdentity ||
            reinterpret_cast<std::uintptr_t>(surface->Surface) != mResourceIdentity ||
            surface->Width != mWidth || surface->Height != mHeight) return false;
        SurfaceLock locked(surface);
        if (!locked.IsValid() || locked.WasLost() || locked.GetRed() != mRed || locked.GetGreen() != mGreen || locked.GetBlue() != mBlue)
            return false;
        // 必须先采齐全部区域，重叠区域此时看到的仍是同一份原生底图。
        if (!CaptureHudDamageBackground(locked.Row(0), locked.GetPitchPixels(), mBounds, clipped, mBackground)) return false;
        mDamage = std::move(clipped);
        mPrepared = true;
        return true;
    }
    std::uintptr_t NativeHudCanvas::GetFontKey() const noexcept
    {
        // 4A5EC8/4A59E4 使用的当前字体实例；只保留身份数值用于测量缓存失效。
        return *reinterpret_cast<const std::uintptr_t*>(0x89C4D0u);
    }
    void NativeHudCanvas::AddDamage(HudRect rect)
    {
        rect = Intersect(rect, mBounds);
        if (rect.mWidth <= 0 || rect.mHeight <= 0) { return; }
        if (!mPrepared || !IsHudRectCovered(rect, mDamage)) throw std::runtime_error("HUD drawing outside prepared background");
    }
    std::uint16_t NativeHudCanvas::PackColor(HudColor color) const noexcept
    {
        return mFormat.Pack(color);
    }
    int NativeHudCanvas::Measure(const std::wstring& text) const
    {
        return CallHudTextMeasure(reinterpret_cast<HudMeasureTextFunction>(
            game::hud_profile::GET_TEXT_DIMENSIONS_ADDRESS), text).mWidth;
    }
    int NativeHudCanvas::GetLineHeight() const
    {
        return (std::max)(12, CallHudTextMeasure(reinterpret_cast<HudMeasureTextFunction>(
            game::hud_profile::GET_TEXT_DIMENSIONS_ADDRESS), L"国Ag").mHeight);
    }
    void NativeHudCanvas::Black(HudRect rect)
    {
        rect = Intersect(rect, mBounds);
        if (rect.mWidth <= 0 || rect.mHeight <= 0) return;
        AddDamage(rect);
        SurfaceLock locked(DSurface::Composite.get());
        if (!locked.IsValid() || locked.WasLost())
        {
            if (locked.WasLost()) mDamage.clear();
            throw std::runtime_error("HUD surface lost during black fill");
        }
        for (int y = rect.mY; y < rect.mY + rect.mHeight; ++y)
        {
            auto* pixels = locked.Row(y);
            for (int x = rect.mX; x < rect.mX + rect.mWidth; ++x) pixels[x] = mFormat.Black(pixels[x]);
        }
    }
    void NativeHudCanvas::Tint(HudRect rect, HudColor color, unsigned int opacity256)
    {
        rect = Intersect(rect, mBounds);
        if (rect.mWidth <= 0 || rect.mHeight <= 0) { return; }
        AddDamage(rect);
        SurfaceLock locked(DSurface::Composite.get());
        if (!locked.IsValid() || locked.WasLost())
        {
            if (locked.WasLost()) mDamage.clear();
            throw std::runtime_error("HUD surface lost during fill");
        }
        const auto foreground = PackColor(color);
        for (int y = rect.mY; y < rect.mY + rect.mHeight; ++y)
        {
            auto* pixels = locked.Row(y);
            for (int x = rect.mX; x < rect.mX + rect.mWidth; ++x)
            {
                // 75% 透明即黑色 25% 不透明；逐通道混合，兼容 RGB565 和 RGB555。
                pixels[x] = mFormat.Blend(pixels[x], foreground, opacity256);
            }
        }
    }
    void NativeHudCanvas::Text(const std::wstring& text, HudRect clip, HudPoint point, HudColor color)
    {
        clip = Intersect(clip, mBounds);
        if (clip.mWidth <= 0 || clip.mHeight <= 0) { return; }
        AddDamage(clip);
        auto* const surface = DSurface::Composite.get();
        // DrawText 接受文字字面量；绝不能将玩家消息送入 DrawFormattedText。
        CallHudTextDraw(reinterpret_cast<HudDrawTextFunction>(game::hud_profile::DRAW_TEXT_ADDRESS),
            surface, text, clip, point, PackColor(color));
    }
    void NativeHudCanvas::Line(HudPoint first, HudPoint second, HudColor color, HudRect clip)
    {
        clip = Intersect(clip, mBounds);
        clip = Intersect(clip, {(std::min)(first.mX, second.mX), (std::min)(first.mY, second.mY),
            std::abs(first.mX - second.mX) + 1, std::abs(first.mY - second.mY) + 1});
        if (clip.mWidth <= 0 || clip.mHeight <= 0) { return; }
        AddDamage(clip);
        auto* const surface = DSurface::Composite.get();
        (void)surface->DrawLineEx(NativeRect(clip), {first.mX, first.mY},
            {second.mX, second.mY}, PackColor(color));
    }
    void NativeHudCanvas::Fade(HudRect rect, float opacity)
    {
        if (opacity >= 1.0f) { return; }
        rect = Intersect(rect, mBounds);
        if (rect.mWidth <= 0 || rect.mHeight <= 0) return;
        AddDamage(rect);
        SurfaceLock locked(DSurface::Composite.get());
        if (!locked.IsValid() || locked.WasLost())
        {
            if (locked.WasLost()) mDamage.clear();
            throw std::runtime_error("HUD surface lost during fade");
        }
        const auto alpha = static_cast<unsigned int>((std::clamp)(opacity, 0.0f, 1.0f) * 256.0f);
        for (int y = rect.mY; y < rect.mY + rect.mHeight; ++y)
        {
            auto* pixels = locked.Row(y);
            const auto offset = static_cast<std::size_t>(y - mBounds.mY) * mBounds.mWidth;
            for (int x = rect.mX; x < rect.mX + rect.mWidth; ++x)
            { pixels[x] = mFormat.Blend(mBackground[offset + x - mBounds.mX], pixels[x], alpha); }
        }
    }
}
