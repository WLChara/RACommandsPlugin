#pragma once
#include <Windows.h>
#include <string>

namespace ra_commands::game
{
    using HudRenderCallback = void(*)();
    using HudMessageCallback = bool(*)(HWND, UINT, WPARAM&, LPARAM);
    /** 仅所属窗口线程安装；桥接终生保留，停用后仍允许恢复最后一帧底图。 */
    bool InstallHudUiHook(HWND window, HudRenderCallback restore,
        HudRenderCallback render, HudMessageCallback input, std::string& outError);
    void DisableHudUiHook() noexcept;
    bool IsHudUiHookEnabled() noexcept;
}
