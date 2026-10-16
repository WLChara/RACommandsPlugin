#pragma once

#include "UI/Overlay/HudGeometry.h"
#include <cstdint>

namespace ra_commands::ui
{
    // 私有的目标 x86 文本 ABI；不经过会改写 EBP 的 SDK JMP_STD 内联包装。
    using HudMeasureTextFunction = HudRect*(__fastcall*)(HudRect*, const wchar_t*,
        HudPoint, std::uint16_t, int, int);
    using HudDrawTextFunction = HudPoint*(__fastcall*)(HudPoint*, const wchar_t*,
        void*, const HudRect*, const HudPoint*, std::uint16_t, std::uint32_t,
        std::uint32_t, std::uint32_t);
    inline constexpr std::uint16_t HUD_FONT_FLAGS = 0x16;

    inline HudRect CallHudTextMeasure(HudMeasureTextFunction measure, const std::wstring& text)
    {
        HudRect dimensions{};
        (void)measure(&dimensions, text.c_str(), {0, 0}, HUD_FONT_FLAGS, 0, 0);
        return dimensions;
    }
    inline void CallHudTextDraw(HudDrawTextFunction draw, void* surface,
        const std::wstring& text, HudRect bounds, HudPoint absolutePoint, std::uint16_t color)
    {
        // 原生 4A5ED3/4A5ED8 会加上 bounds 原点；只在此 ABI 边界转换为相对坐标。
        const HudPoint relativePoint{absolutePoint.mX - bounds.mX, absolutePoint.mY - bounds.mY};
        HudPoint result{};
        (void)draw(&result, text.c_str(), surface, &bounds, &relativePoint, color, 0, HUD_FONT_FLAGS, 1);
    }
}
