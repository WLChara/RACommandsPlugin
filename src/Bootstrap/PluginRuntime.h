#pragma once

namespace ra_commands::bootstrap
{
    // 自动入口在卸载请求后不再启动；显式入口仍可重新初始化。
    bool AutoInitialize();
    bool Initialize();
    bool IsReady();
    void Shutdown();
}
