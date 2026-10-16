#pragma once

#include <Windows.h>
#include <string>

namespace ra_commands::game
{
    /** 在已验证宿主的游戏窗口线程安装统一 HUD 呈现与输入入口。 */
    bool InstallGameHudUi(HWND window, std::string& outError);
    bool IsGameHudUiAvailable() noexcept;
    void UpdateGameHudUi();
    void DisableGameHudUi() noexcept;
}
