#pragma once

#include <cstdint>

namespace ra_commands::game
{
    /** 目标样本 OutList 的剩余槽数；异常索引按零槽处理。仅在游戏线程调用。 */
    [[nodiscard]] std::uint32_t GetNativeEventFreeSlots();
}
