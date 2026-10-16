#pragma once

#include <string>

namespace ra_commands::game
{
    class NativeNetworkEventAdapter;

    enum class ChatSpeedRoute { Unavailable, Local, Network };

    /** 仅战役/遭遇战允许本地写入；未就绪的联机对局绝不回退为本地调速。 */
    constexpr ChatSpeedRoute GetChatSpeedRoute(int gameMode, bool networkReady) noexcept
    {
        if (gameMode == 0 || gameMode == 5) return ChatSpeedRoute::Local;
        if ((gameMode == 3 || gameMode == 4) && networkReady) return ChatSpeedRoute::Network;
        return ChatSpeedRoute::Unavailable;
    }

    constexpr int GetNativeChatSpeed(int playerSpeed) noexcept
    {
        return playerSpeed >= 1 && playerSpeed <= 6 ? 6 - playerSpeed : -1;
    }

    /** Bootstrap 在游戏线程绑定长期存活的共用发送适配器；nullptr 可由停用线程撤销。 */
    void BindGameChatSpeedAdapter(const NativeNetworkEventAdapter* adapter) noexcept;

    /** 仅绑定的游戏线程、有效对局可调用。联机成功只表示入队，不表示已执行。 */
    bool TryChangeGameChatSpeed(int playerSpeed, std::wstring& outFeedback);
}
