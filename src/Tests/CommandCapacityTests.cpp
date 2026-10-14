#include "NetworkEvent/ExtraFrame.h"
#include "NetworkEvent/EventBacklog.h"
#include "NetworkEvent/PlanningEvents.h"

#include <cstring>
#include <stdexcept>

namespace
{
    void RequireCapacity(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    std::uint32_t ReadCapacityDword(const std::uint8_t* bytes)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }
}

void RunCommandCapacityTests()
{
    using namespace ra_commands::network_event;
    const auto base = BuildCapacityMegaMission(2, 33, 2, {123, 52}, {124, 52}, {125, 11});
    RequireCapacity(base.Kind == 4 && base.Timestamp == 33 && base.HouseIndex == 2 &&
        ReadCapacityDword(base.Data.Raw) == 123 && base.Data.Raw[4] == 52 &&
        base.Data.Raw[5] == 2 && base.Data.Raw[6] == 0 &&
        ReadCapacityDword(base.Data.Raw + 7) == 124 && base.Data.Raw[11] == 52 &&
        ReadCapacityDword(base.Data.Raw + 12) == 125 && base.Data.Raw[16] == 11 &&
        ReadCapacityDword(base.Data.Raw + 17) == 123 &&
        base.Data.Raw[21] == 0 && base.Data.Raw[22] == 0,
        "base Mega layout must match native constructor at event Data offset seven");
    const auto fast = BuildCapacityMegaMissionF(2, 34, 1, {126, 52}, {127, 52}, {128, 11}, -9, 400);
    RequireCapacity(fast.Kind == 5 && ReadCapacityDword(fast.Data.Raw) == 126 &&
        fast.Data.Raw[5] == 1 && ReadCapacityDword(fast.Data.Raw + 6) == 127 &&
        ReadCapacityDword(fast.Data.Raw + 11) == 128 &&
        ReadCapacityDword(fast.Data.Raw + 16) == static_cast<std::uint32_t>(-9) &&
        ReadCapacityDword(fast.Data.Raw + 20) == 400,
        "fast Mega layout must preserve unmodified speed and maximum speed bytes");
    for (const auto kind : {6u, 7u, 9u, 10u, 22u})
        RequireCapacity(BuildCapacitySimpleEvent(2, 35, static_cast<std::uint8_t>(kind), {123, 52}).has_value(),
            "verified ordinary clicked event kind must be supported");
    for (const auto kind : {8u, 15u, 16u, 0x60u, 0x61u})
        RequireCapacity(!BuildCapacitySimpleEvent(2, 35, static_cast<std::uint8_t>(kind), {123, 52}),
            "unverified or custom kinds must follow native rules");
    RequireCapacity(!BuildCapacitySimpleEvent(2, 35, 6, {123, 11}) &&
        !BuildCapacitySimpleEvent(2, 35, 6, {0, 52}), "simple actor must have nonzero native techno identity");
    EventBacklog backlog;
    for (std::size_t index = 0; index < COMMAND_BACKLOG_CAPACITY; ++index)
        RequireCapacity(backlog.Submit(base, {1, 4}), "complete declared capacity must be usable");
    RequireCapacity(!backlog.Submit(base, {1, 4}) &&
        backlog.GetStats().mPending == COMMAND_BACKLOG_CAPACITY,
        "physical backlog bound must reject one extra copied event");
    backlog.Clear();
    RequireCapacity(backlog.GetStats().mPending == 0 && backlog.GetStats().mGeneration > 1,
        "clear must invalidate captured generations and free buffered capacity");

    auto planningNode = base;
    RequireCapacity(!IsPlanningQueuedEvent(planningNode), "ordinary flag0 Mega must not enter generic planning capture");
    planningNode.Data.Raw[PLANNING_FLAG_PAYLOAD_OFFSET] = 1;
    RequireCapacity(IsPlanningAppendEvent(planningNode) && IsPlanningQueuedEvent(planningNode),
        "native flag1 append must be recognized after upstream constructor");
    planningNode.Data.Raw[PLANNING_FLAG_PAYLOAD_OFFSET] = 2;
    RequireCapacity(!IsPlanningQueuedEvent(planningNode), "native flag2 deferred execution must keep its original path");
    RequireCapacity(!IsPlanningQueuedEvent(fast), "kind5 must retain its ordinary producer path");
    RequireCapacity(GetPlanningPayloadSize(0x2A) == 10u && GetPlanningPayloadSize(0x2B) == 0u &&
        GetPlanningPayloadSize(0x2C) == 5u && !GetPlanningPayloadSize(0x60),
        "planning controls must use exact native lengths, including valid zero-byte Commit");
    for (const auto kind : {0x2Au, 0x2Bu, 0x2Cu})
    {
        NetworkEvent control{};
        control.Kind = static_cast<std::uint8_t>(kind);
        control.HouseIndex = 2;
        control.Data.Target = {123, 52};
        RequireCapacity(IsPlanningQueuedEvent(control) && !HasCapacityEventActor(control, 123, 52),
            "planning controls must never be reinterpreted as ordinary actor replacements");
    }
}
