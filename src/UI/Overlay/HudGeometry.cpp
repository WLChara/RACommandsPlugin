#include "UI/Overlay/HudGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace ra_commands::ui
{
    bool HudPointerOwnership::Begin(unsigned int button, bool isHit) noexcept
    {
        mButtons &= ~button;
        if (!isHit && !mButtons) { return false; }
        mButtons |= button;
        return true;
    }
    bool HudPointerOwnership::End(unsigned int button) noexcept
    {
        const bool owned = (mButtons & button) != 0;
        mButtons &= ~button;
        return owned;
    }
    void HudPointerOwnership::Cancel() noexcept { mButtons = 0; }
    unsigned int HudPointerOwnership::GetButtons() const noexcept { return mButtons; }
    bool HudRenderHealth::ObserveFailure(std::uint64_t nowMs, bool isVisible) noexcept
    {
        if (!isVisible) { ObserveSuccess(); return false; }
        if (!mHasVisibleFailure) { mHasVisibleFailure = true; mFailureAtMs = nowMs; return false; }
        return nowMs >= mFailureAtMs && nowMs - mFailureAtMs >= 2000;
    }
    void HudRenderHealth::ObserveSuccess() noexcept { mFailureAtMs = 0; mHasVisibleFailure = false; }
    bool Contains(HudRect rect, HudPoint point) noexcept
    {
        return rect.mWidth > 0 && rect.mHeight > 0 && point.mX >= rect.mX &&
            point.mY >= rect.mY && point.mX - rect.mX < rect.mWidth &&
            point.mY - rect.mY < rect.mHeight;
    }
    HudRect Intersect(HudRect first, HudRect second) noexcept
    {
        const int left = (std::max)(first.mX, second.mX);
        const int top = (std::max)(first.mY, second.mY);
        return {left, top, (std::max)(0, (std::min)(first.mX + first.mWidth,
            second.mX + second.mWidth) - left), (std::max)(0,
            (std::min)(first.mY + first.mHeight, second.mY + second.mHeight) - top)};
    }
    HudLayout CalculateHudLayout(HudRect battlefield, bool editing, bool hasChat, bool hasFeed)
    {
        HudLayout layout;
        layout.mBattlefield = battlefield;
        if (battlefield.mWidth < 90 || battlefield.mHeight < 100) { return layout; }
        const int margin = (std::min)(10, battlefield.mWidth / 20);
        const int left = battlefield.mX + margin;
        const int width = (std::min)(440, battlefield.mWidth - margin * 2);
        const int bottom = battlefield.mY + battlefield.mHeight - margin;
        // 左上保留原生通知区；雷达开启时先保留侧栏空间，再限制聊天历史高度。
        const int reservedTop = battlefield.mY + (std::min)(110, battlefield.mHeight / 3);
        const int inputHeight = hasChat && editing ? 32 : 0;
        const int contentBottom = bottom - inputHeight;
        int chatTopLimit = reservedTop;
        if (hasFeed)
        {
            const int feedTop = (std::max)(reservedTop, battlefield.mY + battlefield.mHeight / 3);
            const int available = (std::max)(0, contentBottom - feedTop);
            const int gap = hasChat ? (std::min)(margin, available / 8) : 0;
            const int minimumChat = hasChat ? (std::min)(60, (std::max)(1, available / 3)) : 0;
            const int feedHeight = (std::min)(168, (std::max)(0, available - minimumChat - gap));
            layout.mFeed = {left, feedTop, (std::min)(320, width), feedHeight};
            if (feedHeight > 0) chatTopLimit = feedTop + feedHeight + gap;
        }
        if (hasChat)
        {
            const int available = (std::max)(0, contentBottom - chatTopLimit);
            const int chatHeight = (std::min)(editing ? 310 : 150, available);
            layout.mInput = editing ? HudRect{left, bottom - inputHeight, width, inputHeight} : HudRect{};
            layout.mChat = {left, bottom - inputHeight - chatHeight, width, chatHeight};
        }
        return layout;
    }
    float ChatOpacity(std::uint64_t receivedAtMs, std::uint64_t nowMs) noexcept
    {
        const auto ageMs = nowMs >= receivedAtMs ? nowMs - receivedAtMs : 0;
        if (ageMs <= 10000) { return 1.0f; }
        if (ageMs >= 12000) { return 0.0f; }
        return static_cast<float>(12000 - ageMs) / 2000.0f;
    }
    HudPoint ClientToSurface(HudPoint point, int clientWidth, int clientHeight,
        int surfaceWidth, int surfaceHeight) noexcept
    {
        if (clientWidth <= 0 || clientHeight <= 0) { return {}; }
        return {static_cast<int>(static_cast<std::int64_t>(point.mX) * surfaceWidth / clientWidth),
            static_cast<int>(static_cast<std::int64_t>(point.mY) * surfaceHeight / clientHeight)};
    }
    HudRect CalculateNativeHudCrop(int width, int height, double zoom) noexcept
    {
        if (width <= 0 || height <= 0 || !std::isfinite(zoom) || zoom < 1.0 || zoom > 8.0) { return {}; }
        // 与 DoBlit 4F4780 的 Game::F2I/FISTP 一致，中心和宽高分别按当前舍入模式取整。
        return {static_cast<int>(std::nearbyint((width - width / zoom) * 0.5)),
            static_cast<int>(std::nearbyint((height - height / zoom) * 0.5)),
            static_cast<int>(std::nearbyint(width / zoom)), static_cast<int>(std::nearbyint(height / zoom))};
    }
    HudPoint ClientToHudSurface(HudPoint point, int clientWidth, int clientHeight,
        int windowWidth, int windowHeight, HudRect source, HudRect destination) noexcept
    {
        if (destination.mWidth <= 0 || destination.mHeight <= 0) { return {}; }
        const auto logical = ClientToSurface(point, clientWidth, clientHeight, windowWidth, windowHeight);
        return {source.mX + static_cast<int>(static_cast<std::int64_t>(logical.mX - destination.mX) *
            source.mWidth / destination.mWidth), source.mY + static_cast<int>(
                static_cast<std::int64_t>(logical.mY - destination.mY) * source.mHeight / destination.mHeight)};
    }
    HudPoint HudSurfaceToClient(HudPoint point, int clientWidth, int clientHeight,
        int windowWidth, int windowHeight, HudRect source, HudRect destination) noexcept
    {
        if (source.mWidth <= 0 || source.mHeight <= 0 || windowWidth <= 0 || windowHeight <= 0) { return {}; }
        const int x = destination.mX + static_cast<int>(static_cast<std::int64_t>(point.mX - source.mX) *
            destination.mWidth / source.mWidth);
        const int y = destination.mY + static_cast<int>(static_cast<std::int64_t>(point.mY - source.mY) *
            destination.mHeight / source.mHeight);
        return ClientToSurface({x, y}, windowWidth, windowHeight, clientWidth, clientHeight);
    }
    bool TryPlaceEdgeArrow(HudRect viewport, HudPoint target,
        const std::vector<HudArrow>& occupied, HudArrow& outArrow)
    {
        if (viewport.mWidth < 44 || viewport.mHeight < 44) { return false; }
        const bool isInside = Contains(viewport, target);
        const double centerX = viewport.mX + viewport.mWidth / 2.0;
        const double centerY = viewport.mY + viewport.mHeight / 2.0;
        double deltaX = target.mX - centerX;
        double deltaY = target.mY - centerY;
        double length = std::hypot(deltaX, deltaY);
        if (length < 1.0)
        {
            // 中心重合时没有唯一边缘交点，固定顶部位置，箭头再朝实际中心定向。
            deltaX = 0;
            deltaY = -1;
            length = 1;
        }
        const double unitX = deltaX / length;
        const double unitY = deltaY / length;
        const double halfWidth = viewport.mWidth / 2.0 - 16;
        const double halfHeight = viewport.mHeight / 2.0 - 16;
        const double scale = (std::min)(std::abs(deltaX) > 0.001 ? halfWidth / std::abs(deltaX) : 1e20,
            std::abs(deltaY) > 0.001 ? halfHeight / std::abs(deltaY) : 1e20);
        const int tipX = static_cast<int>(std::lround(centerX + deltaX * scale));
        const int tipY = static_cast<int>(std::lround(centerY + deltaY * scale));
        for (int attempt = 0; attempt < 17; ++attempt)
        {
            const int shift = attempt == 0 ? 0 : ((attempt + 1) / 2 * 20 * (attempt % 2 ? 1 : -1));
            // 沿所命中边缘错位，保证每个完整三角形仍落在视口内。
            const bool verticalEdge = std::abs(tipX - centerX) / halfWidth > 0.99;
            const int x = (std::clamp)(tipX + (verticalEdge ? 0 : shift), viewport.mX + 16,
                viewport.mX + viewport.mWidth - 17);
            const int y = (std::clamp)(tipY + (verticalEdge ? shift : 0), viewport.mY + 16,
                viewport.mY + viewport.mHeight - 17);
            const bool collides = std::any_of(occupied.begin(), occupied.end(), [=](const auto& arrow)
                { return std::abs(arrow.mTip.mX - x) < 18 && std::abs(arrow.mTip.mY - y) < 18; });
            if (collides) { continue; }
            double arrowX = unitX;
            double arrowY = unitY;
            if (isInside)
            {
                // 屏内标记同样显示边缘提示；从当前边缘位置朝实际目标定向。
                const double targetX = target.mX - x;
                const double targetY = target.mY - y;
                const double targetLength = std::hypot(targetX, targetY);
                if (targetLength >= 1.0)
                {
                    arrowX = targetX / targetLength;
                    arrowY = targetY / targetLength;
                }
                else
                {
                    // 目标恰落在箭头锚点时使用朝视口中心的方向，避免除零。
                    arrowX = -unitX;
                    arrowY = -unitY;
                }
            }
            outArrow = {{x, y}, {static_cast<int>(std::lround(x - arrowX * 12 - arrowY * 6)),
                static_cast<int>(std::lround(y - arrowY * 12 + arrowX * 6))},
                {static_cast<int>(std::lround(x - arrowX * 12 + arrowY * 6)),
                static_cast<int>(std::lround(y - arrowY * 12 - arrowX * 6))}};
            return true;
        }
        return false;
    }
    std::vector<std::wstring> WrapHudText(const std::wstring& text,
        int width, int lastLineReserve, const HudMeasure& measure)
    {
        std::vector<std::wstring> lines;
        std::wstring line;
        width = (std::max)(1, width);
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            const wchar_t character = text[index];
            if (character == L'\r') { continue; }
            if (character == L'\n') { lines.push_back(line); line.clear(); continue; }
            std::wstring next = line;
            next += character;
            // Windows UTF-16 代理对始终作为一个字形参与测量和换行。
            if (character >= 0xD800 && character <= 0xDBFF && index + 1 < text.size() &&
                text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDFFF) { next += text[++index]; }
            if (!line.empty() && measure(next) > width)
            {
                lines.push_back(line);
                next.erase(0, line.size());
            }
            line = std::move(next);
        }
        lines.push_back(line);
        if (lastLineReserve > 0 && measure(lines.back()) > (std::max)(0, width - lastLineReserve))
        { lines.emplace_back(); }
        return lines;
    }
    bool IsSupportedPixelMasks(std::uint16_t red, std::uint16_t green, std::uint16_t blue) noexcept
    {
        const auto contiguous = [](unsigned int mask)
        {
            if (!mask) { return false; }
            while (!(mask & 1)) { mask >>= 1; }
            return (mask & (mask + 1)) == 0;
        };
        return contiguous(red) && contiguous(green) && contiguous(blue) &&
            !(red & green) && !(red & blue) && !(green & blue);
    }
    std::uint16_t BlendHudPixel(std::uint16_t background, std::uint16_t foreground,
        unsigned int opacity256, std::uint16_t red, std::uint16_t green, std::uint16_t blue) noexcept
    {
        opacity256 = (std::min)(256u, opacity256);
        unsigned int result = background & ~(red | green | blue);
        for (const unsigned int mask : {red, green, blue})
        {
            unsigned int shift = 0;
            if (!mask) { continue; }
            while (!((mask >> shift) & 1)) { ++shift; }
            const unsigned int back = (background & mask) >> shift;
            const unsigned int front = (foreground & mask) >> shift;
            result |= ((back * (256 - opacity256) + front * opacity256 + 128) >> 8) << shift;
        }
        return static_cast<std::uint16_t>(result);
    }
    bool CaptureHudBackground(const std::uint16_t* pixels, int pitchPixels,
        HudRect bounds, std::vector<std::uint16_t>& outBackground)
    {
        if (!pixels || bounds.mX < 0 || bounds.mY < 0 || bounds.mWidth <= 0 ||
            bounds.mHeight <= 0 || bounds.mWidth > 8192 || bounds.mHeight > 8192 ||
            pitchPixels < bounds.mX + bounds.mWidth) { return false; }
        outBackground.resize(static_cast<std::size_t>(bounds.mWidth) * bounds.mHeight);
        for (int y = 0; y < bounds.mHeight; ++y)
        { std::memcpy(outBackground.data() + static_cast<std::size_t>(y) * bounds.mWidth,
            pixels + static_cast<std::size_t>(y + bounds.mY) * pitchPixels + bounds.mX, bounds.mWidth * 2); }
        return true;
    }
    bool CaptureHudDamageBackground(const std::uint16_t* pixels, int pitchPixels, HudRect bounds,
        const std::vector<HudRect>& damage, std::vector<std::uint16_t>& outBackground)
    {
        if (!pixels || bounds.mX < 0 || bounds.mY < 0 || bounds.mWidth <= 0 || bounds.mHeight <= 0 ||
            bounds.mWidth > 8192 || bounds.mHeight > 8192 || pitchPixels < bounds.mX + bounds.mWidth) return false;
        outBackground.resize(static_cast<std::size_t>(bounds.mWidth) * bounds.mHeight);
        for (const auto rectangle : damage)
        {
            const auto clipped = Intersect(rectangle, bounds);
            for (int y = clipped.mY; y < clipped.mY + clipped.mHeight; ++y)
                std::memcpy(outBackground.data() + static_cast<std::size_t>(y - bounds.mY) * bounds.mWidth + clipped.mX - bounds.mX,
                    pixels + static_cast<std::size_t>(y) * pitchPixels + clipped.mX, clipped.mWidth * 2);
        }
        return true;
    }
    bool CanRestoreHudSurface(const HudSurfaceIdentity& saved, const HudSurfaceIdentity& current, bool wasLost) noexcept
    { return !wasLost && saved.mSurface && saved.mResource && saved.mWidth > 0 && saved.mHeight > 0 && saved == current; }
    bool IsHudRectCovered(HudRect rect, const std::vector<HudRect>& prepared)
    {
        if (rect.mWidth <= 0 || rect.mHeight <= 0) return true;
        bool intersects = false;
        for (const auto area : prepared)
        {
            if (rect.mX >= area.mX && rect.mY >= area.mY && rect.mX + rect.mWidth <= area.mX + area.mWidth &&
                rect.mY + rect.mHeight <= area.mY + area.mHeight) return true;
            const auto cut = Intersect(rect, area);
            intersects |= cut.mWidth > 0 && cut.mHeight > 0;
        }
        if (!intersects) return false;
        std::vector<HudRect> uncovered{rect};
        for (const auto area : prepared)
        {
            std::vector<HudRect> next;
            for (const auto piece : uncovered)
            {
                const auto cut = Intersect(piece, area);
                if (cut.mWidth <= 0 || cut.mHeight <= 0) { next.push_back(piece); continue; }
                if (cut.mY > piece.mY) next.push_back({piece.mX, piece.mY, piece.mWidth, cut.mY - piece.mY});
                if (cut.mY + cut.mHeight < piece.mY + piece.mHeight)
                    next.push_back({piece.mX, cut.mY + cut.mHeight, piece.mWidth, piece.mY + piece.mHeight - cut.mY - cut.mHeight});
                if (cut.mX > piece.mX) next.push_back({piece.mX, cut.mY, cut.mX - piece.mX, cut.mHeight});
                if (cut.mX + cut.mWidth < piece.mX + piece.mWidth)
                    next.push_back({cut.mX + cut.mWidth, cut.mY, piece.mX + piece.mWidth - cut.mX - cut.mWidth, cut.mHeight});
            }
            uncovered = std::move(next);
            if (uncovered.empty()) return true;
        }
        return false;
    }
    bool RestoreHudBackground(std::uint16_t* pixels, int pitchPixels, HudRect bounds,
        const std::vector<std::uint16_t>& background, const std::vector<HudRect>& damage)
    {
        if (!pixels || bounds.mX < 0 || bounds.mY < 0 || bounds.mWidth <= 0 ||
            bounds.mHeight <= 0 || pitchPixels < bounds.mX + bounds.mWidth ||
            background.size() != static_cast<std::size_t>(bounds.mWidth) * bounds.mHeight) { return false; }
        for (const auto rectangle : damage)
        {
            const auto clipped = Intersect(rectangle, bounds);
            for (int y = clipped.mY; y < clipped.mY + clipped.mHeight; ++y)
            {
                const auto offset = static_cast<std::size_t>(y - bounds.mY) * bounds.mWidth + clipped.mX - bounds.mX;
                std::memcpy(pixels + static_cast<std::size_t>(y) * pitchPixels + clipped.mX,
                    background.data() + offset, clipped.mWidth * 2);
            }
        }
        return true;
    }
}
