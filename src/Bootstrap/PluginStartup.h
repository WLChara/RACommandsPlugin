#pragma once

#include <Windows.h>

namespace ra_commands::bootstrap
{
    /**
     * DllMain 的唯一自动入口；只核对映射中的 PE 指纹。
     * 候选游戏 DLL 常驻进程并启动 worker；Syringe 等其他宿主不创建线程、不固定 DLL。
     * 返回 false 表示候选游戏的固定或线程创建失败。
     */
    bool StartPluginFromDllMain() noexcept;
    /** 取消尚未完成的自动启动；不等待 worker，允许外部 Shutdown 先取消再停用 runtime。 */
    void CancelPluginStartup() noexcept;
}
