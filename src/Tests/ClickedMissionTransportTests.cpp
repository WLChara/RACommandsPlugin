#include "ClickedMission/ClickedMissionDispatcher.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace ra_commands::commands;

    class TransportGame final : public IClickedMissionGamePort
    {
    public:
        mutable std::vector<ClickedMissionIntent> mTransport;
        std::uintptr_t mSession = 1;
        std::uint32_t mFrame = 1;

        bool IsMatchReady() const override { return true; }
        std::uintptr_t GetSessionIdentity() const override { return mSession; }
        std::uint32_t GetCurrentFrame() const override { return mFrame; }
        std::uint32_t GetNativeFreeSlots() const override { return 65536; }
        bool ValidateClickedMissionIntent(const ClickedMissionIntent&) const override { return true; }
        void AttemptClickedMission(const ClickedMissionIntent& intent) const override
        { mTransport.push_back(intent); }
        std::size_t CancelSubmittedByProducer(ClickedMissionProducer producer) const override
        {
            return std::erase_if(mTransport, [producer](const auto& pending)
                { return pending.Producer == producer; });
        }
        std::size_t CancelSubmittedByProducerAndActor(ClickedMissionProducer producer,
            const ClickedMissionIdentity& actor) const override
        {
            return std::erase_if(mTransport, [&](const auto& pending)
                { return pending.Producer == producer && pending.Actor == actor; });
        }
        bool HasSubmittedActor(ClickedMissionProducer producer, std::uintptr_t address,
            std::uint32_t uniqueId, std::uint32_t epoch) const override
        {
            return std::any_of(mTransport.begin(), mTransport.end(), [&](const auto& pending)
                { return pending.Producer == producer && pending.Actor.Address == address &&
                    pending.Actor.UniqueId == uniqueId && pending.Actor.Epoch == epoch; });
        }
    };

    void Require(bool condition, const char* message)
    {
        if (!condition) { throw std::runtime_error(message); }
    }
}

void RunClickedMissionTransportTests()
{
    TransportGame game;
    ClickedMissionDispatcher dispatcher(game);
    dispatcher.OnGameFrame();
    ClickedMissionIntent move;
    move.Epoch = dispatcher.Epoch();
    move.Actor = {0x1000, 27, 3, move.Epoch};
    move.Producer = ClickedMissionProducer::AutoFormation;
    move.Supersession = ClickedMissionSupersession::ReplaceSameProducerActorMission;
    move.Mission = 2;
    move.DestinationCell = CellCoordinate{3, 4};
    move.CreatedFrame = game.mFrame;
    Require(dispatcher.Submit(move) == ClickedMissionEnqueueResult::Enqueued,
        "transport fixture move was rejected");
    dispatcher.OnGameFrame();
    Require(game.mTransport.size() == 1 && dispatcher.HasPendingActor(move.Producer, 0x1000, 27),
        "an attempted but unsent transport move must remain pending");

    auto other = move;
    other.Producer = ClickedMissionProducer::AirSpread;
    Require(dispatcher.Submit(other) == ClickedMissionEnqueueResult::Enqueued,
        "independent producer should retain its own transport move");
    dispatcher.OnGameFrame();
    Require(game.mTransport.size() == 2, "independent producers were merged");
    auto newer = move;
    newer.DestinationCell = CellCoordinate{8, 9};
    Require(dispatcher.Submit(newer) == ClickedMissionEnqueueResult::Enqueued &&
        game.mTransport.size() == 1 && game.mTransport.front().Producer == other.Producer,
        "a new goal must replace only its producer's unsent old transport goal");
    dispatcher.OnGameFrame();
    auto stale = newer;
    ++stale.Epoch;
    ++stale.Actor.Epoch;
    Require(dispatcher.Submit(stale) == ClickedMissionEnqueueResult::WrongEpoch &&
        game.mTransport.size() == 2, "a rejected epoch must not cancel valid transport work");
    Require(dispatcher.CancelByProducerAndActor(newer.Producer, newer.Actor) == 1 &&
        game.mTransport.size() == 1 && game.mTransport.front().Producer == other.Producer,
        "actor cancellation leaked into another producer");
    ++game.mSession;
    dispatcher.OnGameFrame();
    Require(game.mTransport.empty(), "session reset retained submitted transport work");
}
