#include "UI/Overlay/HudTextCache.h"
#include <algorithm>
#include <utility>

namespace ra_commands::ui
{
    void HudDraftMetrics::Update(const std::wstring& draft, int width, std::uintptr_t fontKey, const HudMeasure& measure)
    {
        if (mWidth == width && mFontKey == fontKey && mDraft == draft) return;
        std::vector<std::pair<std::size_t, int>> glyphs{{0, 0}};
        std::wstring prefix;
        prefix.reserve(draft.size());
        for (std::size_t index = 0; index < draft.size();)
        {
            const auto character = draft[index++];
            prefix += character;
            if (character >= 0xD800 && character <= 0xDBFF && index < draft.size() &&
                draft[index] >= 0xDC00 && draft[index] <= 0xDFFF) prefix += draft[index++];
            // 完整原生前缀测量保留字距语义，不能改成逐字宽度累加。
            glyphs.push_back({index, measure(prefix)});
        }
        mDraft = draft; mWidth = width; mFontKey = fontKey; mGlyphs = std::move(glyphs);
    }
    int HudDraftMetrics::WidthAt(std::size_t offset) const noexcept
    {
        const auto found = std::upper_bound(mGlyphs.begin(), mGlyphs.end(), offset,
            [](std::size_t value, const auto& glyph) { return value < glyph.first; });
        return found == mGlyphs.begin() ? 0 : (found - 1)->second;
    }
    int HudMeasuredText::Measure(const std::wstring& text, std::uintptr_t fontKey, const HudMeasure& measure)
    {
        if (!mValid || mFontKey != fontKey || mText != text)
        { const int width = measure(text); mText = text; mWidth = width; mFontKey = fontKey; mValid = true; }
        return mWidth;
    }
    const std::vector<std::wstring>& HudWrappedText::Chat(const std::wstring& sender, const std::wstring& body,
        int width, std::uintptr_t fontKey, const HudMeasure& measure)
    {
        if (!mChatFormat || mWidth != width || mFontKey != fontKey || mFirst != sender || mSecond != body || mLines.empty())
        {
            auto lines = WrapHudText(L"<" + sender + L"> " + body, width, 21, measure);
            mFirst = sender; mSecond = body; mWidth = width; mFontKey = fontKey; mLines = std::move(lines); mChatFormat = true;
        }
        return mLines;
    }
    const std::vector<std::wstring>& HudWrappedText::Radar(const std::wstring& label, unsigned int count,
        int width, std::uintptr_t fontKey, const HudMeasure& measure)
    {
        if (mChatFormat || mWidth != width || mFontKey != fontKey || mFirst != label || mCount != count || mLines.empty())
        {
            auto lines = WrapHudText(label + (count > 1 ? L" ×" + std::to_wstring(count) : L""), width, 0, measure);
            mFirst = label; mCount = count; mWidth = width; mFontKey = fontKey; mLines = std::move(lines); mChatFormat = false;
        }
        return mLines;
    }
}
