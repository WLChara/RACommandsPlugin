#include "ClickedMission/ClickedMissionIntentHandlers.h"

#include <stdexcept>

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

    bool Accept(const ClickedMissionIntent&) { return true; }
    bool Reject(const ClickedMissionIntent&) { return false; }
    void Attempt(const ClickedMissionIntent&) {}

    void TestBindingAndRouting()
    {
        ClickedMissionIntentHandlers handlers;
        const ClickedMissionIntentHandler first{&Accept, &Attempt};
        Require(!handlers.Find(ClickedMissionProducer::AutoCrush),
            "unbound producer must have no handler");
        Require(!handlers.Bind(ClickedMissionProducer::Unspecified, first) &&
            !handlers.Bind(ClickedMissionProducer::Count, first) &&
            !handlers.Bind(ClickedMissionProducer::AutoCrush, {nullptr, &Attempt}),
            "invalid bindings must fail closed");
        Require(handlers.Bind(ClickedMissionProducer::AutoCrush, first) &&
            handlers.Bind(ClickedMissionProducer::AutoCrush, first),
            "the same handler must support initialization retry");
        Require(!handlers.Bind(ClickedMissionProducer::AutoCrush,
            {&Reject, &Attempt}),
            "a different handler must not replace a bound producer");
        const auto* const bound = handlers.Find(ClickedMissionProducer::AutoCrush);
        Require(bound && bound->Validate({}) && bound->Attempt == &Attempt &&
            !handlers.Find(ClickedMissionProducer::AirSpread),
            "lookup must isolate producers");
    }
}

void RunClickedMissionIntentHandlersTests()
{
    TestBindingAndRouting();
}
