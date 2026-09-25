#pragma once

#include <atomic>

namespace ra_commands::safe_mode
{
    // 该布尔值用于调整部分功能强度，称为安全模式；默认关闭，仅由切换命令和生命周期重置修改。
    extern std::atomic<bool> g_IsSafeModeEnabled;
}
