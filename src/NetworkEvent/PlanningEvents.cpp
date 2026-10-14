#include "NetworkEvent/PlanningEvents.h"

namespace ra_commands::network_event
{
    bool IsPlanningAppendEvent(const NetworkEvent& event) noexcept
    {
        return event.Kind == PLANNING_MEGA_MISSION_KIND &&
            event.Data.Raw[PLANNING_FLAG_PAYLOAD_OFFSET] == PLANNING_APPEND_FLAG;
    }

    bool IsPlanningControlEvent(std::uint8_t kind) noexcept
    {
        return kind == static_cast<std::uint8_t>(PlanningEventKind::Connect) ||
            kind == static_cast<std::uint8_t>(PlanningEventKind::Commit) ||
            kind == static_cast<std::uint8_t>(PlanningEventKind::NodeDelete);
    }

    bool IsPlanningQueuedEvent(const NetworkEvent& event) noexcept
    {
        return IsPlanningAppendEvent(event) || IsPlanningControlEvent(event.Kind);
    }

    std::optional<std::size_t> GetPlanningPayloadSize(std::uint8_t kind) noexcept
    {
        // 当前样本原生长度表8208EC及构造入口核对：2A/2B/2C的载荷为10/0/5。
        switch (kind)
        {
        case PLANNING_MEGA_MISSION_KIND: return 23;
        case static_cast<std::uint8_t>(PlanningEventKind::Connect): return 10;
        case static_cast<std::uint8_t>(PlanningEventKind::Commit): return 0;
        case static_cast<std::uint8_t>(PlanningEventKind::NodeDelete): return 5;
        default: return std::nullopt;
        }
    }
}
