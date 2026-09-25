#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

namespace ra_commands::game
{
    using AFloorModeQuery = bool(*)();
    using ManualVehicleOrderObserver = void(*)(std::uint64_t actorId);

    /** 仅在目标 EXE 门禁通过、主帧 Hook 已固定 DLL 后安装；失败不影响其他命令。 */
    [[nodiscard]] bool InstallAFloorHooks(AFloorModeQuery isEnabled, std::string& outError);
    /** 仅允许已确认的游戏线程参与本地输入目标替换。 */
    void SetAFloorGameThread(DWORD threadId) noexcept;
    /** 仅观察本地左键调用栈中真正发出的 Move/Attack 类任务。 */
    void SetManualVehicleOrderObserver(ManualVehicleOrderObserver observer) noexcept;
    /** 关闭功能回调但保留已安装的游戏 Hook，DLL 不得在进程结束前卸载。 */
    void DisableAFloorHooks() noexcept;
}
