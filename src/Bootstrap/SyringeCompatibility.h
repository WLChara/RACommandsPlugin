#pragma once

#include <Windows.h>

struct SyringeHandshakeInfo;

namespace ra_commands::bootstrap
{
    /**
     * 在 Syringe 宿主中只读握手字段和工作目录内的 gamemd.exe。
     * 不启动 runtime，不保存调用方缓冲区，不访问游戏地址；失败或异常均返回 S_FALSE。
     */
    HRESULT HandleSyringeHandshake(SyringeHandshakeInfo* info) noexcept;
}
