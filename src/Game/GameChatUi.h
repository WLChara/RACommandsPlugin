#pragma once

#include "UI/Overlay/HudTypes.h"
#include <Windows.h>
#include <cstdint>
#include <string>

namespace ra_commands::game
{
    /** 仅在目标版本验证后、游戏窗口线程调用。失败时保持原生聊天可用。 */
    bool InstallGameChatUi(std::string& outError);
    void UpdateGameChatUi(std::uint64_t nowMs, bool isSessionActive, std::uintptr_t sessionIdentity);
    ui::ChatView CaptureGameChatUi(std::uint64_t nowMs);
    /** 由共用窗口输入入口转交；true 表示该消息已由聊天消费。 */
    bool HandleGameChatUiMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    bool IsGameChatUiEditing() noexcept;
    bool IsGameChatUiEnabled() noexcept;
    void SetGameChatUiEnabled(bool isEnabled);
    bool CopyGameChatUiMessage(HWND window, std::uint64_t messageId);
    void SetGameChatUiCaret(std::size_t caret, bool extendSelection);
    void SetGameChatUiInputPosition(int clientX, int clientY) noexcept;
    void DisableGameChatUi() noexcept;
}
