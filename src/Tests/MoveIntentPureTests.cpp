#include "ClickedMission/ClickedMissionQueue.h"

#include <stdexcept>
#include <vector>

namespace
{
    using namespace ra_commands::commands;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    ClickedMissionIntent MoveIntent(std::int32_t x, std::int32_t y,
        std::uint32_t frame = 100)
    {
        ClickedMissionIntent intent;
        intent.Actor = { 0x1000, 1, 3, 7 };
        intent.Mission = 2; // Mission::Move; pure tests do not depend on the game SDK.
        intent.DestinationCell = CellCoordinate{ x, y };
        intent.Producer = ClickedMissionProducer::AirSpread;
        intent.Supersession = ClickedMissionSupersession::ReplaceSameProducerActorMission;
        intent.Epoch = 7;
        intent.CreatedFrame = frame;
        intent.FrameSendRate = 7;
        return intent;
    }

    void TestMoveDestinationDedupe()
    {
        ClickedMissionQueue queue(3, 7);
        Require(queue.Enqueue(MoveIntent(10, 20)) == ClickedMissionEnqueueResult::Enqueued,
            "first Move destination should enqueue");
        Require(queue.Enqueue(MoveIntent(10, 20, 101)) == ClickedMissionEnqueueResult::Duplicate,
            "same actor and destination should deduplicate");
        Require(queue.Enqueue(MoveIntent(11, 20)) == ClickedMissionEnqueueResult::Replaced,
            "different X must supersede pending Move for the same actor");
        Require(queue.Enqueue(MoveIntent(10, 21)) == ClickedMissionEnqueueResult::Replaced,
            "different Y must supersede pending Move for the same actor");
        auto otherActor = MoveIntent(11, 20);
        otherActor.Actor.UniqueId = 2;
        Require(queue.Enqueue(otherActor) == ClickedMissionEnqueueResult::Enqueued,
            "a different actor must retain its own Move intent");

        std::vector<CellCoordinate> attempted;
        const auto result = queue.Drain(101, [] { return 13u; },
            [](const auto&) { return true; },
            [&](const ClickedMissionIntent& intent)
            {
                attempted.push_back(*intent.DestinationCell);
            });
        Require(result.Attempted == 2 && attempted ==
            std::vector<CellCoordinate>{ { 10, 21 }, { 11, 20 } },
            "only latest destination per actor should retain FIFO order");
    }

    void TestMoveDeadlineAndCapacity()
    {
        ClickedMissionQueue queue(2, 7);
        Require(queue.Enqueue(MoveIntent(10, 20)) == ClickedMissionEnqueueResult::Enqueued,
            "Move should enqueue before deadline test");
        Require(queue.Enqueue(MoveIntent(10, 20, 110)) == ClickedMissionEnqueueResult::Duplicate,
            "duplicate must not extend the original deadline");

        int attempts = 0;
        const auto blocked = queue.Drain(134, [] { return 12u; },
            [](const auto&) { return true; },
            [&](const auto&) { ++attempts; });
        Require(blocked.WasStoppedForCapacity && blocked.Attempted == 0 && queue.Size() == 1,
            "Move must respect the shared 13-slot send threshold");

        const auto expired = queue.Drain(135, [] { return 13u; },
            [](const auto&) { return true; },
            [&](const auto&) { ++attempts; });
        Require(expired.Expired == 1 && attempts == 0 && queue.Size() == 0,
            "Move should expire at age 35 despite a later duplicate");
    }

    void TestCancelOnlyAirSpread()
    {
        ClickedMissionQueue queue(3, 7);
        auto other = MoveIntent(10, 20);
        other.Producer = ClickedMissionProducer::Unspecified;
        Require(queue.Enqueue(MoveIntent(10, 20)) == ClickedMissionEnqueueResult::Enqueued &&
            queue.Enqueue(other) == ClickedMissionEnqueueResult::Enqueued,
            "same actor, Move and cell from different producers must coexist");
        Require(queue.CancelByProducer(ClickedMissionProducer::AirSpread) == 1 &&
            queue.Size() == 1 && queue.Counters().Cancelled == 1,
            "cancelling AirSpread should preserve other producers");

        ClickedMissionProducer attempted = ClickedMissionProducer::AirSpread;
        const auto result = queue.Drain(101, [] { return 13u; },
            [](const auto&) { return true; },
            [&](const ClickedMissionIntent& intent) { attempted = intent.Producer; });
        Require(result.Attempted == 1 && attempted == ClickedMissionProducer::Unspecified,
            "remaining producer should still issue");
    }

    void TestLatestAirSpreadDestinationSupersedesPendingMove()
    {
        ClickedMissionQueue queue(1, 7);
        const auto first = MoveIntent(10, 20, 100);
        const auto latest = MoveIntent(11, 21, 101);
        Require(queue.Enqueue(first) == ClickedMissionEnqueueResult::Enqueued,
            "first Move must enter the queue");
        auto updated = latest;
        updated.Actor = first.Actor;
        Require(queue.Enqueue(updated) == ClickedMissionEnqueueResult::Replaced &&
            queue.Size() == 1 && queue.Counters().Superseded == 1,
            "new destination for same actor must replace pending Move even at capacity");

        CellCoordinate issued{};
        const auto result = queue.Drain(134, [] { return 13u; },
            [](const auto&) { return true; },
            [&](const ClickedMissionIntent& intent) { issued = *intent.DestinationCell; });
        Require(result.Attempted == 1 && issued == *updated.DestinationCell,
            "only the most recent destination may reach native queue");
    }

    void TestAutoCrushMoveReplacementAndActorCancellation()
    {
        ClickedMissionQueue queue(4, 7);
        auto first = MoveIntent(10, 20);
        first.Producer = ClickedMissionProducer::AutoCrush;
        auto latest = first;
        latest.DestinationCell = CellCoordinate{11, 21};
        auto otherActor = first;
        otherActor.Actor.UniqueId = 2;
        auto airSpread = first;
        airSpread.Producer = ClickedMissionProducer::AirSpread;

        Require(queue.Enqueue(first) == ClickedMissionEnqueueResult::Enqueued &&
            queue.Enqueue(otherActor) == ClickedMissionEnqueueResult::Enqueued &&
            queue.Enqueue(airSpread) == ClickedMissionEnqueueResult::Enqueued &&
            queue.Enqueue(latest) == ClickedMissionEnqueueResult::Replaced,
            "auto-crush should replace only its own previous move for one actor");
        Require(queue.Size() == 3 &&
            queue.CancelByProducerAndActor(ClickedMissionProducer::AutoCrush,
                latest.Actor) == 1 && queue.Size() == 2,
            "unmark should cancel only the chosen crusher's pending move");

        std::vector<ClickedMissionIntent> issued;
        queue.Drain(101, [] { return 13u; },
            [](const auto&) { return true; },
            [&](const ClickedMissionIntent& intent) { issued.push_back(intent); });
        Require(issued.size() == 2 &&
            issued[0].Actor.UniqueId == 2 &&
            issued[0].Producer == ClickedMissionProducer::AutoCrush &&
            issued[1].Producer == ClickedMissionProducer::AirSpread,
            "actor cancellation must preserve other actors and producers");
    }

    void TestReplacementIsOptIn()
    {
        ClickedMissionQueue queue(3, 7);
        auto first = MoveIntent(10, 20);
        first.Producer = ClickedMissionProducer::Unspecified;
        first.Supersession = ClickedMissionSupersession::None;
        auto second = first;
        second.DestinationCell = CellCoordinate{11, 20};
        Require(queue.Enqueue(first) == ClickedMissionEnqueueResult::Enqueued &&
            queue.Enqueue(second) == ClickedMissionEnqueueResult::Enqueued &&
            queue.Size() == 2 && queue.Counters().Superseded == 0,
            "producer without replacement policy must retain both intents");
    }
}

void RunMoveIntentTests()
{
    TestMoveDestinationDedupe();
    TestMoveDeadlineAndCapacity();
    TestCancelOnlyAirSpread();
    TestLatestAirSpreadDestinationSupersedesPendingMove();
    TestAutoCrushMoveReplacementAndActorCancellation();
    TestReplacementIsOptIn();
}
