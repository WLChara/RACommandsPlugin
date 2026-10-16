#include "Game/GameHudUi.h"
#include "Game/GameAlertsUi.h"
#include "Game/GameChatUi.h"
#include "Game/GameObjectAccess.h"
#include "Hooks/HudUiHook.h"
#include "UI/Overlay/HudGeometry.h"
#include "UI/Overlay/NativeHudCanvas.h"
#include "UI/Overlay/HudTextCache.h"

#include <YRPPCore.h>
#include <CellClass.h>
#include <GScreenClass.h>
#include <Surface.h>
#include <TacticalClass.h>
#include <Windowsx.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_map>

namespace ra_commands::game
{
    namespace
    {
        using namespace ui;
        constexpr unsigned int LEFT_BUTTON = 1;
        constexpr unsigned int RIGHT_BUTTON = 2;
        constexpr std::uint64_t MAX_HIT_FRAME_AGE_MS = 250;
        struct PlannedBeacon
        {
            HudArrow mArrow;
            HudColor mColor;
            HudRect mBounds;
        };
        struct HudState
        {
            HWND mWindow = nullptr;
            DWORD mOwnerThread = 0;
            NativeHudCanvas mCanvas;
            ChatView mChat;
            std::vector<RadarNotice> mRadar;
            std::vector<BeaconNotice> mBeacons;
            std::unordered_map<std::uint64_t, HudWrappedText> mWrapped;
            std::unordered_map<std::uint64_t, HudWrappedText> mRadarWrapped;
            HudDraftMetrics mDraftMetrics;
            HudMeasuredText mChannelMeasure, mCompositionMeasure;
            std::wstring mChannelLabel, mChannelPrefix;
            std::vector<PlannedBeacon> mPlannedBeacons;
            std::uintptr_t mFontKey = 0;
            std::vector<HudHit> mHits;
            HudPointerOwnership mPointer;
            HudLayout mLayout;
            HudRect mPresentationSource;
            HudRect mPresentationDestination;
            double mPresentationZoom = 1.0;
            int mSurfaceWidth = 0;
            int mSurfaceHeight = 0;
            int mClientWidth = 0;
            int mClientHeight = 0;
            int mChatScrollPixels = 0;
            int mChatMaxScrollPixels = 0;
            int mFeedScrollRows = 0;
            int mLineHeight = 16;
            int mDraftOriginX = 0;
            std::uint64_t mRenderedAtMs = 0;
            HudRenderHealth mRenderHealth;
            bool mCanCompose = false;
            bool mOwnsCapture = false;
            bool mReleasingCapture = false;
            std::atomic_bool mFailed{false};
        };
        HudState g_Hud;

        void ReleaseCaptureOwned() noexcept
        {
            if (!g_Hud.mOwnsCapture) { return; }
            g_Hud.mOwnsCapture = false;
            if (GetCapture() == g_Hud.mWindow)
            {
                g_Hud.mReleasingCapture = true;
                ReleaseCapture();
                g_Hud.mReleasingCapture = false;
            }
        }
        void CancelInput() noexcept
        {
            // 保留已接管按钮的 Up 记账，焦点回来后不让孤立 Up 泄漏给游戏。
            ReleaseCaptureOwned();
            g_Hud.mHits.clear();
            g_Hud.mRenderedAtMs = 0;
        }
        void RecordCanvasFailure()
        {
            g_Hud.mHits.clear();
            g_Hud.mRenderedAtMs = 0;
            const auto nowMs = GetTickCount64();
            // 最小化/Alt-Tab 的丢失表面不是永久失效；可见窗口持续失败则回退原生聊天。
            if (g_Hud.mRenderHealth.ObserveFailure(nowMs, IsWindowVisible(g_Hud.mWindow) &&
                !IsIconic(g_Hud.mWindow) && GetForegroundWindow() == g_Hud.mWindow))
            {
                g_Hud.mFailed = true;
                SetGameChatUiEnabled(false);
                DisableHudUiHook();
                OutputDebugStringA("[RACommandsPlugin] HUD surface unavailable; restored native chat\n");
            }
        }
        void RestoreBackground()
        {
            g_Hud.mCanCompose = false;
            if (!g_Hud.mCanvas.Restore()) { RecordCanvasFailure(); return; }
            g_Hud.mCanCompose = !g_Hud.mFailed && IsHudUiHookEnabled();
            if (!g_Hud.mCanCompose) { g_Hud.mHits.clear(); }
        }
        const std::vector<std::wstring>& GetWrapped(const ChatMessage& message, int width)
        {
            if (g_Hud.mWrapped.size() >= 200 && !g_Hud.mWrapped.contains(message.mId))
                std::erase_if(g_Hud.mWrapped, [](const auto& item)
                { return std::none_of(g_Hud.mChat.mMessages.begin(), g_Hud.mChat.mMessages.end(),
                    [&](const auto& current) { return current.mId == item.first; }); });
            return g_Hud.mWrapped[message.mId].Chat(message.mSender, message.mText, width, g_Hud.mFontKey,
                [](const std::wstring& text) { return g_Hud.mCanvas.Measure(text); });
        }
        HudRect InputStatusRect()
        {
            const auto area = g_Hud.mLayout.mInput;
            return {area.mX, (std::max)(g_Hud.mLayout.mChat.mY, area.mY - g_Hud.mLineHeight - 4),
                area.mWidth, g_Hud.mLineHeight + 4};
        }
        void DrawCopyIcon(HudRect icon, std::uint64_t messageId, bool interactive)
        {
            const HudColor color{215, 225, 235};
            const HudPoint topLeft{icon.mX + 3, icon.mY + 5};
            const HudPoint topRight{icon.mX + 10, icon.mY + 5};
            const HudPoint bottomRight{icon.mX + 10, icon.mY + 13};
            const HudPoint bottomLeft{icon.mX + 3, icon.mY + 13};
            g_Hud.mCanvas.Line(topLeft, topRight, color, icon);
            g_Hud.mCanvas.Line(topRight, bottomRight, color, icon);
            g_Hud.mCanvas.Line(bottomRight, bottomLeft, color, icon);
            g_Hud.mCanvas.Line(bottomLeft, topLeft, color, icon);
            g_Hud.mCanvas.Line({icon.mX + 6, icon.mY + 2}, {icon.mX + 13, icon.mY + 2}, color, icon);
            g_Hud.mCanvas.Line({icon.mX + 13, icon.mY + 2}, {icon.mX + 13, icon.mY + 10}, color, icon);
            if (interactive) { g_Hud.mHits.push_back({icon, HudAction::ChatCopy, messageId}); }
        }
        void DrawChat(std::uint64_t nowMs)
        {
            const auto area = g_Hud.mLayout.mChat;
            if (area.mHeight <= 0) { return; }
            const bool editing = g_Hud.mChat.mEditing;
            int contentHeight = 0;
            for (const auto& message : g_Hud.mChat.mMessages)
            {
                if (!editing && ChatOpacity(message.mReceivedAtMs, nowMs) <= 0) { continue; }
                const auto& lines = GetWrapped(message, area.mWidth - 14);
                contentHeight += static_cast<int>(lines.size()) * g_Hud.mLineHeight + 10;
            }
            g_Hud.mChatMaxScrollPixels = (std::max)(0, contentHeight - area.mHeight);
            g_Hud.mChatScrollPixels = editing ? (std::clamp)(g_Hud.mChatScrollPixels, 0, g_Hud.mChatMaxScrollPixels) : 0;
            if (editing)
            {
                g_Hud.mCanvas.Black(area);
                g_Hud.mHits.push_back({area, HudAction::ChatScroll, 0});
            }
            int bottom = area.mY + area.mHeight + g_Hud.mChatScrollPixels;
            for (auto message = g_Hud.mChat.mMessages.rbegin(); message != g_Hud.mChat.mMessages.rend(); ++message)
            {
                const float opacity = editing ? 1.0f : ChatOpacity(message->mReceivedAtMs, nowMs);
                if (opacity <= 0) { continue; }
                const auto& lines = GetWrapped(*message, area.mWidth - 14);
                const int height = static_cast<int>(lines.size()) * g_Hud.mLineHeight + 8;
                const HudRect card{area.mX, bottom - height, area.mWidth, height};
                const auto clip = Intersect(card, area);
                if (clip.mHeight > 0)
                {
                    if (!editing) { g_Hud.mCanvas.Black(clip); }
                    for (std::size_t index = 0; index < lines.size(); ++index)
                    {
                        const int y = card.mY + 4 + static_cast<int>(index) * g_Hud.mLineHeight;
                        if (y + g_Hud.mLineHeight > clip.mY && y < clip.mY + clip.mHeight)
                        { g_Hud.mCanvas.Text(lines[index], clip, {card.mX + 7, y}, message->mColor); }
                    }
                    if (card.mY + card.mHeight <= area.mY + area.mHeight &&
                        card.mY + card.mHeight - 18 >= area.mY)
                    { DrawCopyIcon({card.mX + card.mWidth - 20, card.mY + card.mHeight - 18, 18, 17}, message->mId, editing); }
                    if (!editing) { g_Hud.mCanvas.Fade(clip, opacity); }
                }
                bottom = card.mY - 2;
                if (bottom < area.mY) { break; }
            }
        }
        void DrawInput(std::uint64_t nowMs)
        {
            const auto area = g_Hud.mLayout.mInput;
            if (!g_Hud.mChat.mEditing || area.mHeight <= 0) { return; }
            const auto& draft = g_Hud.mChat.mDraft;
            const auto caret = (std::min)(draft.size(), g_Hud.mChat.mCaret);
            const auto anchor = (std::min)(draft.size(), g_Hud.mChat.mSelectionAnchor);
            const int y = area.mY + (area.mHeight - g_Hud.mLineHeight) / 2;
            if (g_Hud.mChannelLabel != g_Hud.mChat.mChannelLabel || g_Hud.mChannelPrefix.empty())
            {
                g_Hud.mChannelLabel = g_Hud.mChat.mChannelLabel;
                g_Hud.mChannelPrefix = L"[" + g_Hud.mChannelLabel + L"] ";
            }
            const auto& prefix = g_Hud.mChannelPrefix;
            const HudMeasure measure = [](const std::wstring& text) { return g_Hud.mCanvas.Measure(text); };
            const int labelWidth = (std::min)(area.mWidth / 3, g_Hud.mChannelMeasure.Measure(prefix, g_Hud.mFontKey, measure));
            HudRect textClip{area.mX + labelWidth + 7, area.mY, area.mWidth - labelWidth - 14, area.mHeight};
            g_Hud.mCanvas.Black(area);
            g_Hud.mCanvas.Text(prefix, {area.mX + 5, area.mY, labelWidth, area.mHeight}, {area.mX + 5, y}, {180, 220, 255});
            g_Hud.mDraftMetrics.Update(draft, textClip.mWidth, g_Hud.mFontKey, measure);
            const int caretWidth = g_Hud.mDraftMetrics.WidthAt(caret);
            const int scrollX = (std::max)(0, caretWidth - textClip.mWidth + 18);
            g_Hud.mDraftOriginX = textClip.mX - scrollX;
            if (caret != anchor)
            {
                const int selectionLeft = g_Hud.mDraftMetrics.WidthAt((std::min)(caret, anchor));
                const int selectionRight = g_Hud.mDraftMetrics.WidthAt((std::max)(caret, anchor));
                g_Hud.mCanvas.Tint(Intersect(textClip, {g_Hud.mDraftOriginX + selectionLeft, y,
                    selectionRight - selectionLeft, g_Hud.mLineHeight}), {45, 105, 190}, 190);
            }
            g_Hud.mCanvas.Text(draft, textClip, {g_Hud.mDraftOriginX, y}, {255, 255, 255});
            const int caretX = g_Hud.mDraftOriginX + caretWidth;
            if (!g_Hud.mChat.mComposition.empty())
            {
                g_Hud.mCanvas.Text(g_Hud.mChat.mComposition, textClip, {caretX, y}, {255, 225, 130});
                g_Hud.mCanvas.Line({caretX, y + g_Hud.mLineHeight - 1},
                    {caretX + g_Hud.mCompositionMeasure.Measure(g_Hud.mChat.mComposition, g_Hud.mFontKey, measure), y + g_Hud.mLineHeight - 1},
                    {255, 225, 130}, textClip);
            }
            if ((nowMs / 500) % 2 == 0)
            { g_Hud.mCanvas.Line({caretX, y}, {caretX, y + g_Hud.mLineHeight - 1}, {255, 255, 255}, textClip); }
            if (g_Hud.mSurfaceWidth > 0 && g_Hud.mSurfaceHeight > 0)
            {
                const auto clientCaret = HudSurfaceToClient({caretX, y + g_Hud.mLineHeight},
                    g_Hud.mClientWidth, g_Hud.mClientHeight, g_Hud.mSurfaceWidth, g_Hud.mSurfaceHeight,
                    g_Hud.mPresentationSource, g_Hud.mPresentationDestination);
                SetGameChatUiInputPosition(clientCaret.mX, clientCaret.mY);
            }
            g_Hud.mHits.push_back({area, HudAction::ChatCaret, 0});
            if (!g_Hud.mChat.mStatus.empty())
            {
                const auto status = InputStatusRect();
                g_Hud.mCanvas.Black(status);
                g_Hud.mCanvas.Text(g_Hud.mChat.mStatus, status, {status.mX + 5, status.mY + 2}, {255, 200, 100});
            }
        }
        void DrawRadar()
        {
            const auto area = g_Hud.mLayout.mFeed;
            const int rowHeight = (std::max)(26, g_Hud.mLineHeight + 8);
            if (area.mHeight <= 0 || area.mWidth <= 0) { return; }
            g_Hud.mCanvas.Black(area);
            g_Hud.mHits.push_back({area, HudAction::FeedScroll, 0});
            if (g_Hud.mRadar.empty())
            {
                g_Hud.mFeedScrollRows = 0;
                g_Hud.mCanvas.Text(L"雷达事件 · 暂无事件", area,
                    {area.mX + 6, area.mY + 4}, {185, 205, 225});
                return;
            }
            const int headerHeight = area.mHeight >= rowHeight * 2 ? rowHeight : 0;
            if (headerHeight)
            {
                g_Hud.mCanvas.Text(L"雷达事件 · " + std::to_wstring(g_Hud.mRadar.size()) + L" 条",
                    {area.mX, area.mY, area.mWidth, headerHeight},
                    {area.mX + 6, area.mY + 4}, {185, 205, 225});
            }
            const int count = (std::max)(1, (std::min)(6, (area.mHeight - headerHeight) / rowHeight));
            g_Hud.mFeedScrollRows = (std::clamp)(g_Hud.mFeedScrollRows, 0,
                (std::max)(0, static_cast<int>(g_Hud.mRadar.size()) - count));
            for (int row = 0; row < count; ++row)
            {
                const int index = g_Hud.mFeedScrollRows + row;
                if (index >= static_cast<int>(g_Hud.mRadar.size())) { break; }
                const auto& notice = g_Hud.mRadar[index];
                const int top = area.mY + headerHeight + row * rowHeight;
                HudRect rectangle{area.mX, top, area.mWidth,
                    (std::min)(rowHeight, area.mY + area.mHeight - top)};
                if (rectangle.mHeight <= 0) break;
                const HudRect pin{rectangle.mX + rectangle.mWidth - 30, rectangle.mY, 30, rectangle.mHeight};
                if (g_Hud.mRadarWrapped.size() >= 128 && !g_Hud.mRadarWrapped.contains(notice.mId))
                    std::erase_if(g_Hud.mRadarWrapped, [](const auto& item)
                    { return std::none_of(g_Hud.mRadar.begin(), g_Hud.mRadar.end(),
                        [&](const auto& current) { return current.mId == item.first; }); });
                const auto& lines = g_Hud.mRadarWrapped[notice.mId].Radar(notice.mLabel, notice.mCount,
                    rectangle.mWidth - 40, g_Hud.mFontKey,
                    [](const auto& text) { return g_Hud.mCanvas.Measure(text); });
                g_Hud.mCanvas.Text(lines.front(), {rectangle.mX + 6, rectangle.mY, rectangle.mWidth - 40, rectangle.mHeight},
                    {rectangle.mX + 6, rectangle.mY + 4}, notice.mRead ? HudColor{175, 175, 175} : HudColor{255, 230, 155});
                g_Hud.mCanvas.Text(notice.mPinned ? L"◆" : L"◇", pin, {pin.mX + 6, pin.mY + 4}, {225, 235, 255});
                g_Hud.mHits.push_back({rectangle, HudAction::RadarJump, notice.mId});
                g_Hud.mHits.push_back({pin, HudAction::RadarPin, notice.mId});
            }
        }
        void PlanBeacons(std::uint64_t nowMs)
        {
            g_Hud.mPlannedBeacons.clear();
            if (!AreBeaconIndicatorsEnabled()) return;
            auto* const tactical = TacticalClass::Instance.get();
            if (!tactical) { return; }
            const auto view = DSurface::ViewBounds.get();
            std::vector<HudArrow> arrows;
            for (const auto& notice : g_Hud.mBeacons)
            {
                if (notice.mExpiresAtMs <= nowMs) { continue; }
                const CoordStruct world{notice.mPosition.mX, notice.mPosition.mY, notice.mPosition.mZ};
                Point2D projected{};
                // 返回 false 表示离屏/遮挡；原生函数已先写出投影，仍须用于边缘方向。
                (void)tactical->CoordsToClient(world, &projected);
                const HudPoint absolute{projected.X + view.X, projected.Y + view.Y};
                HudArrow arrow;
                if (!TryPlaceEdgeArrow(g_Hud.mLayout.mBattlefield, absolute, arrows, arrow)) { continue; }
                arrows.push_back(arrow);
                const int left = (std::min)({arrow.mTip.mX, arrow.mLeft.mX, arrow.mRight.mX});
                const int top = (std::min)({arrow.mTip.mY, arrow.mLeft.mY, arrow.mRight.mY});
                const int right = (std::max)({arrow.mTip.mX, arrow.mLeft.mX, arrow.mRight.mX});
                const int bottom = (std::max)({arrow.mTip.mY, arrow.mLeft.mY, arrow.mRight.mY});
                g_Hud.mPlannedBeacons.push_back({arrow, notice.mColor, {left, top, right - left + 1, bottom - top + 1}});
            }
        }
        void DrawBeacons()
        {
            for (const auto& beacon : g_Hud.mPlannedBeacons)
            {
                const auto& arrow = beacon.mArrow;
                const auto bounds = g_Hud.mLayout.mBattlefield;
                g_Hud.mCanvas.Line(arrow.mTip, arrow.mLeft, beacon.mColor, bounds);
                g_Hud.mCanvas.Line(arrow.mTip, arrow.mRight, beacon.mColor, bounds);
                g_Hud.mCanvas.Line(arrow.mLeft, arrow.mRight, beacon.mColor, bounds);
                const HudPoint center{(arrow.mTip.mX + arrow.mLeft.mX + arrow.mRight.mX) / 3,
                    (arrow.mTip.mY + arrow.mLeft.mY + arrow.mRight.mY) / 3};
                g_Hud.mCanvas.Line(arrow.mTip, center, beacon.mColor, bounds);
            }
        }
        void RenderHudFrame()
        {
            g_Hud.mHits.clear();
            g_Hud.mRenderedAtMs = 0;
            if (!g_Hud.mCanCompose || !IsGameSessionReady()) { return; }
            const auto nowMs = GetTickCount64();
            const bool visibleChat = g_Hud.mChat.mEnabled && (g_Hud.mChat.mEditing ||
                std::any_of(g_Hud.mChat.mMessages.begin(), g_Hud.mChat.mMessages.end(),
                    [=](const auto& message) { return ChatOpacity(message.mReceivedAtMs, nowMs) > 0; }));
            const bool visibleBeacons = AreBeaconIndicatorsEnabled() &&
                std::any_of(g_Hud.mBeacons.begin(), g_Hud.mBeacons.end(),
                    [=](const auto& notice) { return notice.mExpiresAtMs > nowMs; });
            if (!visibleChat && !IsRadarFeedEnabled() && !visibleBeacons)
            { g_Hud.mRenderHealth.ObserveSuccess(); return; }
            HudRect battlefield;
            if (!g_Hud.mCanvas.Begin(battlefield, g_Hud.mSurfaceWidth, g_Hud.mSurfaceHeight))
            { RecordCanvasFailure(); return; }
            auto* const tactical = TacticalClass::Instance.get();
            g_Hud.mPresentationZoom = tactical ? tactical->ZoomInFactor : 1.0;
            g_Hud.mPresentationDestination = {0, 0, g_Hud.mSurfaceWidth, g_Hud.mSurfaceHeight};
            g_Hud.mPresentationSource = CalculateNativeHudCrop(g_Hud.mSurfaceWidth,
                g_Hud.mSurfaceHeight, g_Hud.mPresentationZoom);
            if (g_Hud.mPresentationSource.mWidth <= 0 || g_Hud.mPresentationSource.mHeight <= 0)
            { RecordCanvasFailure(); return; }
            battlefield = Intersect(battlefield, g_Hud.mPresentationSource);
            // Composite 仅含战场宽度，右侧栏另行提交；客户区映射必须使用整窗逻辑尺寸。
            // WinMain 6BDDC3..6BDE14 明确以 WindowBounds.Width - sidebarWidth 创建 Composite。
            const auto windowBounds = DSurface::WindowBounds.get();
            if (windowBounds.Width <= 0 || windowBounds.Height <= 0) { RecordCanvasFailure(); return; }
            g_Hud.mSurfaceWidth = windowBounds.Width;
            g_Hud.mSurfaceHeight = windowBounds.Height;
            RECT client{};
            if (!GetClientRect(g_Hud.mWindow, &client) || client.right <= 0 || client.bottom <= 0) { return; }
            g_Hud.mClientWidth = client.right;
            g_Hud.mClientHeight = client.bottom;
            const auto fontKey = g_Hud.mCanvas.GetFontKey();
            const int lineHeight = g_Hud.mCanvas.GetLineHeight();
            if (g_Hud.mFontKey != fontKey || g_Hud.mLineHeight != lineHeight)
            {
                g_Hud.mWrapped.clear(); g_Hud.mRadarWrapped.clear();
                g_Hud.mDraftMetrics = {}; g_Hud.mChannelMeasure = {}; g_Hud.mCompositionMeasure = {};
            }
            g_Hud.mFontKey = fontKey;
            g_Hud.mLineHeight = lineHeight;
            g_Hud.mLayout = CalculateHudLayout(battlefield, g_Hud.mChat.mEditing,
                g_Hud.mChat.mEnabled, IsRadarFeedEnabled());
            if (g_Hud.mChat.mEnabled && g_Hud.mLayout.mChat.mHeight <= 0)
            { RecordCanvasFailure(); return; }
            try
            {
                std::vector<HudRect> damage;
                if (g_Hud.mLayout.mFeed.mHeight > 0) damage.push_back(g_Hud.mLayout.mFeed);
                if (visibleChat) damage.push_back(g_Hud.mLayout.mChat);
                if (g_Hud.mChat.mEnabled && g_Hud.mChat.mEditing)
                {
                    damage.push_back(g_Hud.mLayout.mInput);
                    if (!g_Hud.mChat.mStatus.empty()) damage.push_back(InputStatusRect());
                }
                PlanBeacons(nowMs);
                for (const auto& beacon : g_Hud.mPlannedBeacons) damage.push_back(beacon.mBounds);
                if (!g_Hud.mCanvas.PrepareDamage(damage)) { RecordCanvasFailure(); return; }
                DrawRadar();
                if (visibleChat) DrawChat(nowMs);
                if (g_Hud.mChat.mEnabled) DrawInput(nowMs);
                DrawBeacons();
            }
            catch (...)
            { RecordCanvasFailure(); return; }
            g_Hud.mRenderHealth.ObserveSuccess();
            g_Hud.mRenderedAtMs = nowMs;
        }
        void RenderHud()
        {
            try { RenderHudFrame(); }
            catch (...) { RecordCanvasFailure(); }
        }
        const HudHit* FindHit(HudPoint point)
        {
            for (auto hit = g_Hud.mHits.rbegin(); hit != g_Hud.mHits.rend(); ++hit)
            { if (Contains(hit->mBounds, point)) { return &*hit; } }
            return nullptr;
        }
        bool IsCurrentFrame()
        {
            RECT client{};
            auto* const tactical = TacticalClass::Instance.get();
            const auto screen = GScreenClass::Instance.get();
            const auto windowBounds = DSurface::WindowBounds.get();
            return g_Hud.mRenderedAtMs && GetTickCount64() - g_Hud.mRenderedAtMs <= MAX_HIT_FRAME_AGE_MS &&
                GetClientRect(g_Hud.mWindow, &client) && client.right == g_Hud.mClientWidth &&
                client.bottom == g_Hud.mClientHeight &&
                IsGameChatUiEditing() == g_Hud.mChat.mEditing && IsGameSessionReady() &&
                windowBounds.Width == g_Hud.mSurfaceWidth && windowBounds.Height == g_Hud.mSurfaceHeight &&
                (!tactical || tactical->ZoomInFactor == g_Hud.mPresentationZoom) &&
                (!screen || (!screen->ScreenShakeX && !screen->ScreenShakeY));
        }
        void ActivateHit(HWND window, const HudHit& hit, HudPoint point, bool rightButton)
        {
            switch (hit.mAction)
            {
            case HudAction::RadarJump:
                if (rightButton) { (void)ToggleRadarNoticePin(hit.mId); }
                else { (void)TryJumpToRadarNotice(hit.mId); }
                break;
            case HudAction::RadarPin: (void)ToggleRadarNoticePin(hit.mId); break;
            case HudAction::ChatCopy:
                if (!rightButton) { (void)CopyGameChatUiMessage(window, hit.mId); }
                break;
            case HudAction::ChatCaret:
                if (!rightButton && !g_Hud.mDraftMetrics.GetGlyphs().empty())
                {
                    const auto& glyphs = g_Hud.mDraftMetrics.GetGlyphs();
                    const int relativeX = point.mX - g_Hud.mDraftOriginX;
                    const auto best = std::min_element(glyphs.begin(), glyphs.end(),
                        [=](const auto& first, const auto& second)
                        { return std::abs(first.second - relativeX) < std::abs(second.second - relativeX); });
                    SetGameChatUiCaret(best->first, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
                    g_Hud.mRenderedAtMs = 0;
                }
                break;
            default: break;
            }
        }
        bool HandleHudInput(HWND window, UINT message, WPARAM& flags, LPARAM coordinates)
        {
            const bool enabled = IsHudUiHookEnabled() && !g_Hud.mFailed;
            if (HandleGameChatUiMessage(window, message, flags, coordinates)) { return true; }
            if (message == WM_KILLFOCUS || message == WM_CANCELMODE || message == WM_NCDESTROY ||
                (message == WM_ACTIVATEAPP && !flags) ||
                (message == WM_CAPTURECHANGED && !g_Hud.mReleasingCapture && reinterpret_cast<HWND>(coordinates) != window))
            { CancelInput(); return false; }
            const bool leftUp = message == WM_LBUTTONUP;
            const bool rightUp = message == WM_RBUTTONUP;
            if (leftUp || rightUp)
            {
                if (!g_Hud.mPointer.End(leftUp ? LEFT_BUTTON : RIGHT_BUTTON)) { return false; }
                if (!g_Hud.mPointer.GetButtons()) { ReleaseCaptureOwned(); }
                return true;
            }
            if (message == WM_MOUSEMOVE)
            {
                if (g_Hud.mPointer.GetButtons() & LEFT_BUTTON) { flags &= ~static_cast<WPARAM>(MK_LBUTTON); }
                if (g_Hud.mPointer.GetButtons() & RIGHT_BUTTON) { flags &= ~static_cast<WPARAM>(MK_RBUTTON); }
                return g_Hud.mOwnsCapture;
            }
            if (!enabled) { return false; }
            const bool leftDown = message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK;
            const bool rightDown = message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK;
            if (!leftDown && !rightDown && message != WM_MOUSEWHEEL) { return false; }
            POINT clientPoint{GET_X_LPARAM(coordinates), GET_Y_LPARAM(coordinates)};
            if (message == WM_MOUSEWHEEL && !ScreenToClient(window, &clientPoint)) { return false; }
            const auto point = ClientToHudSurface({clientPoint.x, clientPoint.y}, g_Hud.mClientWidth,
                g_Hud.mClientHeight, g_Hud.mSurfaceWidth, g_Hud.mSurfaceHeight,
                g_Hud.mPresentationSource, g_Hud.mPresentationDestination);
            const bool currentFrame = IsCurrentFrame();
            const auto* observedHit = FindHit(point);
            const auto* hit = currentFrame ? observedHit : nullptr;
            if (message == WM_MOUSEWHEEL)
            {
                if (!hit) { return false; }
                const int delta = GET_WHEEL_DELTA_WPARAM(flags);
                if (g_Hud.mChat.mEditing && (hit->mAction == HudAction::ChatScroll ||
                    hit->mAction == HudAction::ChatCopy || hit->mAction == HudAction::ChatCaret))
                {
                    g_Hud.mChatScrollPixels = (std::clamp)(g_Hud.mChatScrollPixels +
                        delta * g_Hud.mLineHeight * 3 / WHEEL_DELTA, 0, g_Hud.mChatMaxScrollPixels);
                    return true;
                }
                if (hit->mAction == HudAction::RadarJump || hit->mAction == HudAction::RadarPin ||
                    hit->mAction == HudAction::FeedScroll)
                { g_Hud.mFeedScrollRows = (std::max)(0, g_Hud.mFeedScrollRows - delta / WHEEL_DELTA); return true; }
                return false;
            }
            const unsigned int button = leftDown ? LEFT_BUTTON : RIGHT_BUTTON;
            // 已由原生窗口接管的另一键必须保留成对事件，避免卡住原生拖拽。
            const unsigned int otherButton = leftDown ? RIGHT_BUTTON : LEFT_BUTTON;
            const bool unmanagedOther = (flags & (leftDown ? MK_RBUTTON : MK_LBUTTON)) &&
                !(g_Hud.mPointer.GetButtons() & otherButton);
            if (unmanagedOther || (!g_Hud.mOwnsCapture && GetCapture() != nullptr)) { return false; }
            const bool editingBattlefield = IsGameChatUiEditing() &&
                (!currentFrame || Contains(g_Hud.mLayout.mBattlefield, point));
            // 分辨率/震屏过渡只保留已显示 UI 的保护区，不执行过期布局上的动作。
            if (!g_Hud.mPointer.Begin(button, observedHit || editingBattlefield)) { return false; }
            if (!g_Hud.mOwnsCapture)
            {
                SetCapture(window);
                g_Hud.mOwnsCapture = GetCapture() == window;
            }
            // 捕获失败仍须吞掉本次 Down/Up，但不执行用户动作。
            if (hit && g_Hud.mOwnsCapture) { const auto value = *hit; ActivateHit(window, value, point, rightDown); }
            return true;
        }
    }
    bool InstallGameHudUi(HWND window, std::string& outError)
    {
        if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId())
        { outError = "HUD installation requires the game window thread"; return false; }
        if (!g_Hud.mCanvas.Validate())
        { outError = "HUD requires a lockable native 16-bit composite surface with verified RGB masks"; return false; }
        g_Hud.mWindow = window;
        g_Hud.mOwnerThread = GetCurrentThreadId();
        g_Hud.mFailed = false;
        return InstallHudUiHook(window, &RestoreBackground, &RenderHud, &HandleHudInput, outError);
    }
    void UpdateGameHudUi()
    {
        if (GetCurrentThreadId() != g_Hud.mOwnerThread || !IsHudUiHookEnabled()) { return; }
        const auto nowMs = GetTickCount64();
        g_Hud.mChat = CaptureGameChatUi(nowMs);
        g_Hud.mRadar = CaptureRadarNotices();
        g_Hud.mBeacons = CaptureBeaconNotices(nowMs);
        if (g_Hud.mWrapped.size() > g_Hud.mChat.mMessages.size())
        {
            for (auto item = g_Hud.mWrapped.begin(); item != g_Hud.mWrapped.end();)
            {
                const auto id = item->first;
                if (std::none_of(g_Hud.mChat.mMessages.begin(), g_Hud.mChat.mMessages.end(),
                    [=](const auto& message) { return message.mId == id; })) { item = g_Hud.mWrapped.erase(item); }
                else { ++item; }
            }
        }
        if (g_Hud.mRadarWrapped.size() > g_Hud.mRadar.size())
            std::erase_if(g_Hud.mRadarWrapped, [](const auto& item)
            { return std::none_of(g_Hud.mRadar.begin(), g_Hud.mRadar.end(),
                [&](const auto& current) { return current.mId == item.first; }); });
        if (!IsGameSessionReady())
        {
            g_Hud.mHits.clear();
            g_Hud.mRenderedAtMs = 0;
            g_Hud.mChatScrollPixels = 0;
            g_Hud.mFeedScrollRows = 0;
            g_Hud.mWrapped.clear();
            g_Hud.mRadarWrapped.clear();
            g_Hud.mDraftMetrics = {};
            g_Hud.mChannelMeasure = {};
            g_Hud.mCompositionMeasure = {};
            g_Hud.mPlannedBeacons.clear();
            CancelInput();
        }
    }
    void DisableGameHudUi() noexcept
    {
        // Shutdown 可能来自插件线程；捕获清理由 Hook 投递所属窗口线程，不能同步等待。
        DisableHudUiHook();
    }
    bool IsGameHudUiAvailable() noexcept
    {
        return IsHudUiHookEnabled() && !g_Hud.mFailed.load(std::memory_order_acquire);
    }
}
