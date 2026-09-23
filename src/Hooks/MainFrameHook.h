#pragma once

#include <string>

namespace ra_commands::game
{
    using GameFrameCallback = void(*)();

    /**
     * 仅在目标版本验证通过后安装。成功安装会将 DLL 固定到进程结束；
     * 禁用回调不撤销 Detour，也不允许随后调用 FreeLibrary 卸载本 DLL。
     */
    bool InstallMainFrameHook(GameFrameCallback callback, std::string& outError);
    void DisableMainFrameCallback();
}
