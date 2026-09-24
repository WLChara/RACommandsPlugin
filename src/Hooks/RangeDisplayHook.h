#pragma once

#include <Windows.h>

#include <string>

namespace ra_commands::game
{
    using RangeDisplayModeQuery = bool(*)();

    /** 只在目标 EXE 门禁及原生渲染入口验证成功后安装。 */
    [[nodiscard]] bool InstallRangeDisplayHook(
        RangeDisplayModeQuery isEnabled, std::string& outError);
    void SetRangeDisplayGameThread(DWORD threadId) noexcept;
    /** 停用插件绘制回调；Hook 和 DLL 均保持到进程退出。 */
    void DisableRangeDisplayHook() noexcept;
}
