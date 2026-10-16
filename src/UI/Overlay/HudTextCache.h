#pragma once
#include "UI/Overlay/HudGeometry.h"

namespace ra_commands::ui
{
    class HudDraftMetrics
    {
    public:
        void Update(const std::wstring& draft, int width, std::uintptr_t fontKey, const HudMeasure& measure);
        int WidthAt(std::size_t offset) const noexcept;
        const std::vector<std::pair<std::size_t, int>>& GetGlyphs() const noexcept { return mGlyphs; }
    private:
        std::wstring mDraft;
        int mWidth = -1;
        std::uintptr_t mFontKey = 0;
        std::vector<std::pair<std::size_t, int>> mGlyphs;
    };
    class HudMeasuredText
    {
    public:
        int Measure(const std::wstring& text, std::uintptr_t fontKey, const HudMeasure& measure);
    private:
        std::wstring mText;
        int mWidth = 0;
        std::uintptr_t mFontKey = 0;
        bool mValid = false;
    };
    /** 单条文字缓存，不按消息 ID 推断内容未变，匿名名称变化也会失效。 */
    class HudWrappedText
    {
    public:
        const std::vector<std::wstring>& Chat(const std::wstring& sender, const std::wstring& body,
            int width, std::uintptr_t fontKey, const HudMeasure& measure);
        const std::vector<std::wstring>& Radar(const std::wstring& label, unsigned int count,
            int width, std::uintptr_t fontKey, const HudMeasure& measure);
    private:
        std::wstring mFirst, mSecond;
        unsigned int mCount = 0;
        int mWidth = -1;
        std::uintptr_t mFontKey = 0;
        std::vector<std::wstring> mLines;
        bool mChatFormat = false;
    };
}
