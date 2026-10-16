#include "ChatCommand/ChatCommandState.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>

namespace ra_commands::chat_command
{
    namespace
    {
        constexpr std::size_t MAX_SELF_MASK_UNITS = 32;
        struct Token
        {
            std::wstring_view mValue;
            bool mQuoted = false;
        };
        struct SelectorResult
        {
            std::vector<PlayerId> mPlayers;
            std::wstring mError;
        };
        bool IsSpace(wchar_t character) noexcept
        {
            // 显式采用 Unicode WhiteSpace，避免受 C locale 影响；BOM/ZWSP 常随粘贴进入。
            // 这些不可见前缀不能把 /msg 等本地命令降级为普通广播。
            return (character >= L'\t' && character <= L'\r') || character == L' ' ||
                character == L'\u0085' || character == L'\u00A0' || character == L'\u1680' ||
                (character >= L'\u2000' && character <= L'\u200B') ||
                character == L'\u2028' || character == L'\u2029' || character == L'\u202F' ||
                character == L'\u205F' || character == L'\u3000' || character == L'\uFEFF';
        }
        std::wstring_view Trim(std::wstring_view text) noexcept
        {
            while (!text.empty() && IsSpace(text.front())) { text.remove_prefix(1); }
            while (!text.empty() && IsSpace(text.back())) { text.remove_suffix(1); }
            return text;
        }
        CommandResult Feedback(std::wstring text, bool succeeded = false)
        { return {CommandAction::Feedback, succeeded, std::move(text), {}, std::nullopt, 0}; }

        bool ReadToken(std::wstring_view& input, Token& outToken, std::wstring& outError)
        {
            while (!input.empty() && IsSpace(input.front())) { input.remove_prefix(1); }
            outToken = {};
            if (input.empty()) { return false; }
            if (input.front() == L'"')
            {
                outToken.mQuoted = true;
                input.remove_prefix(1);
                const auto closing = input.find(L'"');
                if (closing == std::wstring_view::npos)
                { outError = L"双引号没有闭合，请使用完整的双引号包围玩家名称。"; return false; }
                outToken.mValue = input.substr(0, closing);
                input.remove_prefix(closing + 1);
                if (!input.empty() && !IsSpace(input.front()))
                { outError = L"双引号后的内容必须用空格分隔。"; return false; }
                return true;
            }
            std::size_t end = 0;
            while (end < input.size() && !IsSpace(input[end]))
            {
                if (input[end] == L'"')
                { outError = L"双引号必须放在玩家名称的开头和结尾。"; return false; }
                ++end;
            }
            outToken.mValue = input.substr(0, end);
            input.remove_prefix(end);
            return true;
        }
        bool IsAsciiDigits(std::wstring_view text) noexcept
        {
            return !text.empty() && std::all_of(text.begin(), text.end(),
                [](wchar_t character) { return character >= L'0' && character <= L'9'; });
        }
        bool IsNonAsciiDigits(std::wstring_view text) noexcept
        {
            // 常见 Unicode 十进制数字仅用于诊断；编号和速度始终只接受 ASCII 数字。
            constexpr wchar_t DIGIT_STARTS[] = {0x0660, 0x06F0, 0x0966, 0x09E6,
                0x0E50, 0x0ED0, 0x1040, 0x17E0, 0xFF10};
            bool hasNonAscii = false;
            for (const auto character : text)
            {
                if (character >= L'0' && character <= L'9') { continue; }
                if (!std::any_of(std::begin(DIGIT_STARTS), std::end(DIGIT_STARTS), [=](wchar_t start)
                    { return character >= start && character <= start + 9; })) { return false; }
                hasNonAscii = true;
            }
            return hasNonAscii;
        }
        bool ParseUnsigned(std::wstring_view text, std::uint64_t maximum, std::uint64_t& outValue) noexcept
        {
            if (!IsAsciiDigits(text)) { return false; }
            std::uint64_t value = 0;
            for (const auto character : text)
            {
                const auto digit = static_cast<std::uint64_t>(character - L'0');
                if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10)) { return false; }
                value = value * 10 + digit;
            }
            outValue = value;
            return true;
        }
        const Player* FindPlayer(const PlayerRoster& roster, PlayerId id) noexcept
        {
            const auto found = std::find_if(roster.begin(), roster.end(), [=](const Player& player)
                { return player.mId == id && id >= 0; });
            return found == roster.end() ? nullptr : &*found;
        }
        SelectorResult ResolveSelector(Token token, const PlayerRoster& roster, std::size_t randomChoice)
        {
            if (Trim(token.mValue).empty()) { return {{}, L"玩家目标不能为空。"}; }
            if (!token.mQuoted && token.mValue.front() == L'@')
            {
                if (token.mValue != L"@a" && token.mValue != L"@s" && token.mValue != L"@r" &&
                    token.mValue != L"@e" && token.mValue != L"@f")
                { return {{}, L"未知玩家选择器，可用 @a、@s、@r、@e、@f。"}; }
                SelectorResult result;
                for (const auto& player : roster)
                {
                    if (player.mId < 0) { continue; }
                    const bool selected = token.mValue == L"@a" || token.mValue == L"@r" ||
                        (token.mValue == L"@s" && player.mIsLocal) ||
                        (token.mValue == L"@e" && !player.mIsLocal && !player.mIsAlly) ||
                        (token.mValue == L"@f" && !player.mIsLocal && player.mIsAlly);
                    if (selected) { result.mPlayers.push_back(player.mId); }
                }
                if (result.mPlayers.empty()) { result.mError = L"该选择器没有匹配到当前人类玩家。"; }
                else if (token.mValue == L"@r")
                {
                    const auto chosen = result.mPlayers[randomChoice % result.mPlayers.size()];
                    result.mPlayers.assign(1, chosen);
                }
                return result;
            }
            if (!token.mQuoted && IsNonAsciiDigits(token.mValue))
            { return {{}, L"玩家编号请使用 ASCII 数字；数字名称请加双引号。"}; }
            if (!token.mQuoted && IsAsciiDigits(token.mValue))
            {
                std::uint64_t number = 0;
                constexpr auto MAX_NUMBER = static_cast<std::uint64_t>((std::numeric_limits<PlayerId>::max)()) + 1;
                if (!ParseUnsigned(token.mValue, MAX_NUMBER, number)) { return {{}, L"玩家编号超出有效整数范围。"}; }
                if (!number) { return {{}, L"玩家编号从 1 开始。"}; }
                const auto id = static_cast<PlayerId>(number - 1);
                return FindPlayer(roster, id) ? SelectorResult{{id}, {}} : SelectorResult{{}, L"找不到该编号对应的当前人类玩家。"};
            }
            SelectorResult result;
            for (const auto& player : roster)
            { if (player.mId >= 0 && player.mName == token.mValue) { result.mPlayers.push_back(player.mId); } }
            if (result.mPlayers.empty()) { result.mError = L"找不到该名称对应的当前人类玩家。"; }
            else if (result.mPlayers.size() != 1)
            {
                result.mPlayers.clear();
                result.mError = L"该玩家名称不唯一，请改用玩家编号。";
            }
            return result;
        }
        std::wstring PlayerNumbers(const std::vector<PlayerId>& players)
        {
            std::wstring text;
            for (const auto id : players)
            {
                if (!text.empty()) { text += L"、"; }
                text += L"#" + std::to_wstring(static_cast<std::int64_t>(id) + 1);
            }
            return text;
        }
        bool IsMaskControl(wchar_t character) noexcept
        {
            return character < 32 || (character >= 127 && character <= 159) ||
                character == L'\u2028' || character == L'\u2029';
        }
        std::wstring ValidateMaskName(std::wstring_view name)
        {
            if (Trim(name).empty()) return L"显示名不能为空。";
            if (name.size() > MAX_SELF_MASK_UNITS) return L"显示名最多包含 32 个 UTF-16 单元，请缩短后重试。";
            for (std::size_t index = 0; index < name.size(); ++index)
            {
                const auto character = name[index];
                if (IsMaskControl(character)) return L"显示名不能包含控制符或换行。";
                if (character == L'<' || character == L'>') return L"显示名不能包含 < 或 >，以免破坏聊天消息格式。";
                if (character >= 0xD800 && character <= 0xDBFF)
                {
                    if (index + 1 >= name.size() || name[index + 1] < 0xDC00 || name[index + 1] > 0xDFFF)
                        return L"显示名包含不完整的 UTF-16 字符，请重新输入。";
                    ++index;
                }
                else if ((character >= 0xDC00 && character <= 0xDFFF) || static_cast<std::uint32_t>(character) > 0xFFFF)
                    return L"显示名包含不完整的 UTF-16 字符，请重新输入。";
            }
            return {};
        }
    }
    const std::wstring ChatCommandState::mDefaultSelfMaskName = L"Player";
    bool IsChatCommandDraft(std::wstring_view draft) noexcept
    {
        const auto remaining = Trim(draft);
        return !remaining.empty() && remaining.front() == L'/';
    }
    void ChatCommandState::Reset() noexcept { mIgnored.clear(); mSelfMasked = false; mSelfMaskName.clear(); }
    bool ChatCommandState::IsIgnored(PlayerId id) const noexcept { return mIgnored.find(id) != mIgnored.end(); }
    bool ChatCommandState::IsSelfMasked() const noexcept { return mSelfMasked; }
    const std::wstring& ChatCommandState::GetSelfMaskName() const noexcept
    { return mSelfMaskName.empty() ? mDefaultSelfMaskName : mSelfMaskName; }

    CommandResult ChatCommandState::Execute(std::wstring_view draft, const PlayerRoster& roster, std::size_t randomChoice)
    {
        auto remaining = draft;
        if (Trim(remaining).empty()) { return Feedback(L"请输入聊天内容或命令。"); }
        if (!IsChatCommandDraft(draft))
        { return {CommandAction::OrdinaryChat, true, {}, std::wstring(draft), std::nullopt, 0}; }
        Token command;
        std::wstring error;
        if (!ReadToken(remaining, command, error)) { return Feedback(std::move(error)); }
        if (command.mValue == L"/setmask")
        {
            // 保留原始参数到此处，先拒绝控制/换行，不能由 Trim 静默去掉非法尾部。
            if (std::any_of(remaining.begin(), remaining.end(), IsMaskControl))
                return Feedback(L"显示名不能包含控制符或换行。");
            auto name = Trim(remaining);
            if (!name.empty() && name.front() == L'"')
            {
                Token quoted;
                if (!ReadToken(name, quoted, error)) return Feedback(std::move(error));
                if (!Trim(name).empty()) return Feedback(L"请用一对双引号包围整个显示名，或直接输入完整显示名。");
                name = quoted.mValue;
            }
            else if (name.find(L'"') != std::wstring_view::npos)
                return Feedback(L"双引号必须成对包围整个显示名。");
            error = ValidateMaskName(name);
            if (!error.empty()) return Feedback(std::move(error));
            mSelfMaskName.assign(name);
            mSelfMasked = true;
            return Feedback(L"本机显示名已设置，姓名隐藏已开启。", true);
        }
        if (command.mValue == L"/selfmask")
        {
            if (!Trim(remaining).empty()) { return Feedback(L"用法：/selfmask，不接受额外参数。"); }
            mSelfMasked = !mSelfMasked;
            return Feedback(mSelfMasked ? L"本机姓名隐藏已开启。" : L"本机姓名隐藏已关闭。", true);
        }
        if (command.mValue == L"/speed")
        {
            Token speed;
            if (!ReadToken(remaining, speed, error))
            { return Feedback(error.empty() ? L"用法：/speed <1..6>。" : std::move(error)); }
            if (!Trim(remaining).empty()) { return Feedback(L"用法：/speed <1..6>，只能提供一个整数。"); }
            std::uint64_t number = 0;
            if (speed.mQuoted || !ParseUnsigned(speed.mValue, 6, number) || number < 1)
            { return Feedback(L"速度必须是 1 到 6 之间的 ASCII 整数。"); }
            return {CommandAction::ChangeSpeed, true, {}, {}, std::nullopt, static_cast<int>(number)};
        }
        if (command.mValue != L"/ignore" && command.mValue != L"/msg")
        { return Feedback(L"未知聊天命令。可用命令：/ignore、/selfmask、/setmask、/speed、/msg。"); }
        Token selector;
        if (!ReadToken(remaining, selector, error))
        {
            return Feedback(!error.empty() ? std::move(error) : command.mValue == L"/msg" ?
                L"用法：/msg <唯一玩家目标> <消息正文>，仅发送一次私聊。" : L"用法：/ignore <玩家目标>。");
        }
        if (command.mValue == L"/ignore" && !Trim(remaining).empty())
        { return Feedback(L"用法：/ignore <玩家目标>，只能提供一个目标或选择器。"); }
        const auto text = Trim(remaining);
        if (command.mValue == L"/msg" && text.empty())
        { return Feedback(L"用法：/msg <唯一玩家目标> <消息正文>，正文不能为空。"); }
        auto selected = ResolveSelector(selector, roster, randomChoice);
        if (!selected.mError.empty()) { return Feedback(std::move(selected.mError)); }
        if (command.mValue == L"/msg")
        {
            if (selected.mPlayers.size() != 1)
            { return Feedback(L"私聊必须且只能匹配一名玩家，请使用唯一名称或玩家编号。"); }
            return {CommandAction::SendPrivate, true, {}, std::wstring(text), selected.mPlayers.front(), 0};
        }
        std::vector<PlayerId> ignored;
        bool skippedSelf = false;
        for (const auto id : selected.mPlayers)
        {
            const auto* player = FindPlayer(roster, id);
            if (player && player->mIsLocal) { skippedSelf = true; continue; }
            ignored.push_back(id);
        }
        if (ignored.empty()) { return Feedback(L"不能屏蔽自己。"); }
        // 在校验全部目标后才修改状态；重复屏蔽是幂等的，不改变其他玩家的记录。
        mIgnored.insert(ignored.begin(), ignored.end());
        return Feedback(L"已屏蔽玩家 " + PlayerNumbers(ignored) + (skippedSelf ? L"（已跳过自己）。" : L"。"), true);
    }
}
