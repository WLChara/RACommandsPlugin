#include "Commands/SafeModeToggleCommand/SafeModeState.h"

namespace ra_commands::safe_mode
{
    // 该布尔值用于调整部分功能强度，称为安全模式。
    std::atomic<bool> g_IsSafeModeEnabled{false};
}
