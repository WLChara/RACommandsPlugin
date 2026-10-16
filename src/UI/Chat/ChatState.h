#pragma once

#include "UI/Overlay/HudTypes.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ra_commands::ui::chat
{
    inline constexpr std::size_t MAX_DRAFT_UNITS = 97;
    inline constexpr std::size_t MAX_HISTORY_MESSAGES = 200;
    inline constexpr std::size_t MAX_SUBMITTED_DRAFTS = 100;

    enum class SubmitRoute { LocalOnly, NativeTransport };
    constexpr SubmitRoute GetSubmitRoute(int gameMode) noexcept
    {
        return gameMode == 0 || gameMode == 5 ? SubmitRoute::LocalOnly : SubmitRoute::NativeTransport;
    }

    /** 保留操作系统窗口切换/关闭入口；普通游戏组合键仍由编辑器消费。键值沿用 Win32 VK 数值。 */
    constexpr bool ShouldPassSystemShortcut(std::uint32_t virtualKey, bool alt, bool control) noexcept
    {
        constexpr std::uint32_t KEY_TAB = 0x09, KEY_ESCAPE = 0x1B, KEY_SPACE = 0x20;
        constexpr std::uint32_t KEY_F4 = 0x73, KEY_LEFT_WINDOWS = 0x5B, KEY_RIGHT_WINDOWS = 0x5C;
        return virtualKey == KEY_LEFT_WINDOWS || virtualKey == KEY_RIGHT_WINDOWS ||
            (alt && (virtualKey == KEY_TAB || virtualKey == KEY_F4 || virtualKey == KEY_ESCAPE || virtualKey == KEY_SPACE)) ||
            (control && virtualKey == KEY_ESCAPE);
    }

    /** 单线程聊天状态；所有字符串和历史均由本对象拥有，不引用游戏缓冲区。 */
    class ChatState
    {
    public:
        void Begin(std::wstring channel);
        void End() noexcept;
        void Reset() noexcept;
        bool IsEditing() const noexcept { return mEditing; }
        bool HasComposition() const noexcept { return !mComposition.empty(); }
        bool Insert(std::wstring_view text);
        bool Backspace();
        bool Delete();
        void Move(int direction, bool extendSelection);
        void MoveToEnd(bool end, bool extendSelection);
        void SelectAll() noexcept;
        void SetCaret(std::size_t position, bool extendSelection) noexcept;
        std::wstring CopySelection() const;
        void SetComposition(std::wstring text);
        void SetStatus(std::wstring text);
        const std::wstring& GetDraft() const noexcept { return mDraft; }
        /**
         * 调用方按提交结果记录本机草稿；不从收到的消息推断输入历史。
         * 只接收满足现有编辑器编码和长度约束的原文；空/无效记录忽略，连续重复去重。
         * 新提交复位导航；Begin/End 保留记录，Reset 换局清除。
         */
        void RecordSubmittedDraft(std::wstring_view draft);
        /** 有组合输入时不召回；编辑已召回内容仍可浏览，越过最新条目恢复首次召回前的草稿。 */
        bool RecallPreviousDraft();
        bool RecallNextDraft();
        bool Append(std::wstring sender, std::wstring text, HudColor color, std::uint64_t nowMs,
            int playerId = -1, bool isLocal = false);
        /** 匿名仅作用于显示快照和复制结果，不改写历史身份或发送内容。 */
        std::wstring CopyMessage(std::uint64_t id, bool selfMask = false, std::wstring_view maskName = L"Player") const;
        ChatView Capture(bool isEnabled, bool selfMask = false, std::wstring_view maskName = L"Player") const;

    private:
        bool EraseSelection();
        void ResetRecallNavigation() noexcept;
        void ApplyRecalledDraft(std::wstring draft);
        std::wstring mDraft;
        std::wstring mComposition;
        std::wstring mChannel;
        std::wstring mStatus;
        std::size_t mCaret = 0;
        std::size_t mAnchor = 0;
        bool mEditing = false;
        std::uint64_t mNextId = 1;
        std::vector<ChatMessage> mMessages;
        std::vector<std::wstring> mSubmittedDrafts;
        std::optional<std::size_t> mRecallIndex;
        std::wstring mRecallDraft;
    };
}
