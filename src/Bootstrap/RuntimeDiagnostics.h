#pragma once

#include <string_view>

namespace ra_commands::bootstrap
{
    /** 在DllMain之外记录启动/注册的阶段事件；诊断失败不影响初始化结果。 */
    void TraceRuntime(std::string_view event) noexcept;
    /** 记录宿主、实际载入DLL路径与本次构建信息，供区分旧进程和错误注入目标。 */
    void TraceRuntimeStartup() noexcept;
}
