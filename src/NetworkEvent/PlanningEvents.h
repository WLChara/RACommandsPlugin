#pragma once

#include "NetworkEvent/NetworkEvent.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace ra_commands::network_event
{
    enum class PlanningEventKind : std::uint8_t
    {
        Connect = 0x2A,
        Commit = 0x2B,
        NodeDelete = 0x2C
    };

    constexpr std::uint8_t PLANNING_MEGA_MISSION_KIND = 4;
    constexpr std::size_t PLANNING_FLAG_PAYLOAD_OFFSET = 22;
    constexpr std::uint8_t PLANNING_APPEND_FLAG = 1;

    [[nodiscard]] bool IsPlanningAppendEvent(const NetworkEvent& event) noexcept;
    [[nodiscard]] bool IsPlanningControlEvent(std::uint8_t kind) noexcept;
    /** 共享6521C0仅接追加节点及控制事件，不接管原生flag0/2执行规则。 */
    [[nodiscard]] bool IsPlanningQueuedEvent(const NetworkEvent& event) noexcept;
    /**
     * 返回原生压缩载荷长度；MegaMission长度不随flag变化。
     * Commit的有效长度为0，与未知kind的nullopt不同，不含Kind或Mega聚合数量字节。
     */
    [[nodiscard]] std::optional<std::size_t> GetPlanningPayloadSize(std::uint8_t kind) noexcept;
}
