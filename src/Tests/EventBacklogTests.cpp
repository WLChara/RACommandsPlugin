#include "NetworkEvent/EventBacklog.h"
#include "NetworkEvent/ExtraFrame.h"
#include "NetworkEvent/PlanningEvents.h"

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    void RequireBacklog(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    ra_commands::network_event::NetworkEvent MakeBacklogEvent(std::uint32_t actorId)
    {
        using namespace ra_commands::network_event;
        return BuildCapacityMegaMission(2, 10, 2, {actorId, 52}, {}, {500, 11});
    }
}

void RunEventBacklogTests()
{
    using namespace ra_commands::network_event;
    for (const std::size_t count : {127u, 128u, 129u, 257u, 1000u})
    {
        EventBacklog backlog;
        for (std::size_t index = 0; index < count; ++index)
            RequireBacklog(backlog.Submit(MakeBacklogEvent(static_cast<std::uint32_t>(index + 1)), {3, 8}),
                "large batches must enter the dynamic backlog");
        std::size_t consumed = 0;
        std::uint32_t frame = 20;
        while (backlog.GetStats().mPending)
        {
            const auto snapshot = backlog.BeginSnapshot(frame++);
            const auto events = backlog.CopyPrefix(snapshot, 256);
            RequireBacklog(backlog.ConsumePrefix(snapshot, events.size(),
                [&](const std::vector<NetworkEvent>& batch) {
                    for (const auto& event : batch)
                        RequireBacklog(HasCapacityEventActor(event, static_cast<std::uint32_t>(++consumed), 52),
                            "multi-frame batches must preserve FIFO actor order");
                    return true;
                }), "valid snapshot prefix must be consumed");
        }
        RequireBacklog(consumed == count, "all copied records must survive multiple batches");
    }

    EventBacklog backlog(3);
    RequireBacklog(backlog.Submit(MakeBacklogEvent(1), {1, 20}) &&
        backlog.Submit(MakeBacklogEvent(1), {2, 20}) &&
        backlog.Submit(MakeBacklogEvent(2), {1, 21}), "three producer records must fit");
    RequireBacklog(!backlog.Submit(MakeBacklogEvent(3), {}), "bounded backlog must reject overflow");
    RequireBacklog(backlog.HasProducerActor(1, 1, 52, 20) &&
        !backlog.HasProducerActor(1, 1, 52, 21) && !backlog.HasProducerActor(1, 1, 11, 20),
        "pending query must match producer, native target kind, and intent epoch");
    RequireBacklog(backlog.CancelProducerActor(1, 1, 52) == 1 &&
        backlog.HasProducerActor(2, 1, 52, 20), "producer actor cancellation must spare other producers");
    RequireBacklog(backlog.CancelActor(1, 52) == 1 && backlog.CancelProducer(1) == 1,
        "manual actor and producer cancellation must clear their remaining scopes");

    RequireBacklog(backlog.Submit(MakeBacklogEvent(4), {}), "fresh event must fit after cancel");
    const auto snapshot = backlog.BeginSnapshot(30);
    RequireBacklog(!backlog.ConsumePrefix(snapshot, 1, [](const auto&) { return false; }) &&
        backlog.GetStats().mPending == 1, "rejected submission must preserve the entire prefix");
    RequireBacklog(backlog.ConsumePrefix(snapshot, 1, [&](const auto&) {
        RequireBacklog(backlog.Submit(MakeBacklogEvent(5), {}), "same-thread reentry must be admitted");
        RequireBacklog(!backlog.BeginSnapshot(30).mValid, "reentry cannot start a nested snapshot");
        return true;
    }), "outer callback must finish");
    RequireBacklog(backlog.GetStats().mPending == 1 && !backlog.BeginSnapshot(30).mValid,
        "reentrant event must wait for a new logical frame");
    const auto next = backlog.BeginSnapshot(31);
    RequireBacklog(backlog.CopyPrefix(next, 1).size() == 1, "next frame must expose deferred record");
    RequireBacklog(backlog.ConsumePrefix(next, 1, [&](const auto&) {
        backlog.Clear();
        RequireBacklog(backlog.Submit(MakeBacklogEvent(6), {}), "clear reentry must accept new generation");
        return true;
    }) && backlog.GetStats().mPending == 1 && !backlog.BeginSnapshot(31).mValid,
        "clear must not consume new generation or promote it in the same frame");
    RequireBacklog(backlog.CopyPrefix(next, 3).empty(), "stale generation snapshots must be rejected");

    const auto cancelSnapshot = backlog.BeginSnapshot(32);
    RequireBacklog(backlog.ConsumePrefix(cancelSnapshot, 1, [&](const auto&) {
        backlog.CancelActor(6, 52);
        RequireBacklog(backlog.Submit(MakeBacklogEvent(7), {}), "cancel reentry must accept a later sequence");
        return true;
    }) && backlog.HasActor(7, 52), "committing a cancelled prefix must spare later sequences");

    std::atomic<bool> clearStarted{false};
    std::atomic<bool> clearFinished{false};
    std::thread worker;
    const auto executionSnapshot = backlog.BeginSnapshot(33);
    RequireBacklog(backlog.ConsumePrefix(executionSnapshot, 1, [&](const auto&) {
        worker = std::thread([&]() {
            clearStarted = true;
            backlog.Clear();
            clearFinished = true;
        });
        while (!clearStarted.load()) std::this_thread::yield();
        RequireBacklog(!clearFinished.load(), "cross-thread clear must wait for the current submission gate");
        return true;
    }), "execution-gated prefix must finish");
    worker.join();
    RequireBacklog(clearFinished && backlog.GetStats().mPending == 0,
        "clear must finish after native submission returns");

    backlog.Submit(MakeBacklogEvent(8), {});
    const auto throwingSnapshot = backlog.BeginSnapshot(34);
    try
    {
        backlog.ConsumePrefix(throwingSnapshot, 1, [](const auto&) -> bool {
            throw std::runtime_error("fixture failure before native submission");
        });
        RequireBacklog(false, "throwing callback should escape to its runtime guard");
    }
    catch (const std::runtime_error&) {}
    RequireBacklog(backlog.GetStats().mPending == 1 && backlog.BeginSnapshot(35).mValid,
        "exception must reset execution state without dropping an unsubmitted record");

    EventBacklog planned;
    auto firstNode = MakeBacklogEvent(40);
    firstNode.Data.Raw[PLANNING_FLAG_PAYLOAD_OFFSET] = 1;
    firstNode.Data.Raw[17] = 0xAA;
    firstNode.Data.Raw[21] = 11;
    auto secondNode = firstNode;
    secondNode.Data.Raw[12] = 0xAB;
    auto ordinaryMove = MakeBacklogEvent(40);
    NetworkEvent connect{};
    connect.Kind = static_cast<std::uint8_t>(PlanningEventKind::Connect);
    connect.HouseIndex = 2;
    connect.Data.Target = {40, 52};
    NetworkEvent commit{};
    commit.Kind = static_cast<std::uint8_t>(PlanningEventKind::Commit);
    commit.HouseIndex = 2;
    NetworkEvent deletion = connect;
    deletion.Kind = static_cast<std::uint8_t>(PlanningEventKind::NodeDelete);
    RequireBacklog(planned.Submit(firstNode, {}) && planned.Submit(ordinaryMove, {}) &&
        planned.Submit(secondNode, {}) && planned.Submit(connect, {}) &&
        planned.Submit(commit, {}) && planned.Submit(deletion, {}),
        "node append and all control kinds must share the existing queue");
    RequireBacklog(planned.CancelActor(40, 52) == 1 && planned.HasActor(40, 52) &&
        planned.GetStats().mPending == 5,
        "ordinary manual replacement must spare earlier flag1 nodes and all controls");
    const auto planSnapshot = planned.BeginSnapshot(100);
    const auto planEvents = planned.CopyPrefix(planSnapshot, 5);
    RequireBacklog(planEvents.size() == 5 && planEvents[0].Data.Raw[17] == 0xAA &&
        planEvents[0].Data.Raw[21] == 11 && planEvents[1].Data.Raw[12] == 0xAB &&
        planEvents[2].Kind == 0x2A && planEvents[3].Kind == 0x2B && planEvents[4].Kind == 0x2C,
        "complete following references, repeated actor nodes, and control FIFO must remain intact");

    EventBacklog largePlan;
    for (int node = 0; node < 2; ++node)
    {
        for (std::uint32_t actor = 1; actor <= 129; ++actor)
        {
            auto event = MakeBacklogEvent(actor);
            event.Data.Raw[PLANNING_FLAG_PAYLOAD_OFFSET] = 1;
            event.Data.Raw[12] = static_cast<std::uint8_t>(node + 1);
            RequireBacklog(largePlan.Submit(event, {}), "129 participant nodes must not hit the original 128 outgoing bound");
        }
    }
    RequireBacklog(largePlan.Submit(commit, {}), "immediate Commit must fit behind all appended nodes");
    std::vector<NetworkEvent> delivered;
    for (std::uint32_t frame = 200; largePlan.GetStats().mPending; ++frame)
    {
        const auto largeSnapshot = largePlan.BeginSnapshot(frame);
        const auto prefix = largePlan.CopyPrefix(largeSnapshot, 128);
        RequireBacklog(largePlan.ConsumePrefix(largeSnapshot, prefix.size(), [&](const auto& events) {
            delivered.insert(delivered.end(), events.begin(), events.end());
            return true;
        }), "bounded planning prefix must remain consumable across frames");
    }
    RequireBacklog(delivered.size() == 259 && delivered.back().Kind == 0x2B,
        "Commit must follow all 258 copied nodes instead of overtaking an unsent batch");
    for (std::size_t index = 0; index < 258; ++index)
        RequireBacklog(HasCapacityEventActor(delivered[index], static_cast<std::uint32_t>(index % 129 + 1), 52) &&
            delivered[index].Data.Raw[12] == index / 129 + 1,
            "each participant must retain both original ordered planning nodes");
}
