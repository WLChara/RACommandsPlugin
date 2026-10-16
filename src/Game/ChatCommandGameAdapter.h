#pragma once

#include "ChatCommand/ChatCommandState.h"
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace ra_commands::game
{
    /** 以下接口只在已验证版本的游戏窗口线程、有效对局内调用；快照拥有字符串，不缓存原生对象。 */
    chat_command::PlayerRoster CaptureChatCommandPlayers();
    int GetLocalChatPlayerId() noexcept;
    /** 名称按原生低字节编码及颜色唯一匹配；未知/歧义返回 -1，不代表协议具备身份认证。 */
    int ResolveIncomingChatPlayer(std::wstring_view wireName, int playerColor);
    std::size_t NextChatCommandRandomChoice();
    /** 只为单次原生提交临时选择一名远端人类；失败绝不调用广播替代。
     * send 必须同步调用原生提交，返回值只表示是否提交；此接口不承诺网络送达。
     * 当前频道和八个收件人标志在正常、失败及异常退出时原样恢复。
     */
    bool WithPrivateChatRecipient(int playerId, const std::function<bool()>& send, std::wstring& outError);
}
