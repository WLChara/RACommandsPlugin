#pragma once

#include <cstdint>
#include <optional>

namespace ra_commands::game
{
    struct CursorCell
    {
        std::int32_t X = 0;
        std::int32_t Y = 0;
    };

    /** 仅在游戏线程读取热键触发瞬间鼠标指向的有效地图格。 */
    [[nodiscard]] std::optional<CursorCell> CaptureCursorCell();
}
