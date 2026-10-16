#pragma once

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ra_commands::chat_command
{
    using PlayerId = int;

    struct Player
    {
        PlayerId mId = -1;
        std::wstring mName;
        bool mIsLocal = false;
        bool mIsAlly = false;
    };
    /** 调用方只提供当前实际人类玩家；非负 ID 唯一，数字显示值为 ID + 1。 */
    using PlayerRoster = std::vector<Player>;

    enum class CommandAction { OrdinaryChat, Feedback, SendPrivate, ChangeSpeed };
    struct CommandResult
    {
        CommandAction mAction = CommandAction::Feedback;
        bool mSucceeded = false;
        std::wstring mFeedback;
        std::wstring mText;
        std::optional<PlayerId> mRecipient;
        int mSpeed = 0;
    };

    /**
     * 判断去除前导 Unicode WhiteSpace、BOM、ZWSP 后是否以 ASCII '/' 开头。
     * 与 Execute 使用相同边界规则；不修改正文，空白输入返回 false。
     */
    bool IsChatCommandDraft(std::wstring_view draft) noexcept;

    /**
     * 本局聊天命令状态。由调用线程独占，Reset 在对局更替时清空屏蔽及隐藏姓名偏好。
     * 不发送消息、不设置游戏速度、不获取随机数，也不持有原生对象或 HUD 状态。
     */
    class ChatCommandState
    {
    public:
        void Reset() noexcept;
        bool IsIgnored(PlayerId id) const noexcept;
        bool IsSelfMasked() const noexcept;
        const std::wstring& GetSelfMaskName() const noexcept;
        /**
         * 返回值只表示命令解析/本地状态操作结果，发送和速度变更仍由调用方执行。
         * @r 从整个 roster（含自己）选择，randomChoice 由外部提供并按候选数取余。
         * /msg 仅支持一次性私聊；正文取目标后的原文并去掉两端空白，不递归解析命令。
         * 失败和所有斜杠命令均不会退化为 OrdinaryChat；反馈统一使用编号避免泄露本机姓名。
         */
        CommandResult Execute(std::wstring_view draft, const PlayerRoster& roster,
            std::size_t randomChoice = 0);
    private:
        std::set<PlayerId> mIgnored;
        bool mSelfMasked = false;
        static const std::wstring mDefaultSelfMaskName;
        std::wstring mSelfMaskName;
    };
}
