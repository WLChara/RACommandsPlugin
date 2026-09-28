#pragma once

#include <string_view>

namespace ra_commands::game
{
    /** 仅在游戏线程低频记录列队计划/进度；UTF-8 日志位于本 DLL 同目录。 */
    void TraceAutoFormation(std::string_view event) noexcept;
}
