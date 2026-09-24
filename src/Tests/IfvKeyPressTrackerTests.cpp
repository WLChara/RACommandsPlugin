#include "Commands/IfvModeSelectCommand/IfvKeyPressTracker.h"

#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
}

void RunIfvKeyPressTrackerTests()
{
    using ra_commands::ifv_select::IfvKeyPressTracker;
    using ra_commands::ifv_select::PressPhase;

    IfvKeyPressTracker tracker;
    Require(tracker.Consume(0x41) == PressPhase::FirstPress,
        "initial keydown must be first press");
    Require(tracker.Consume(0x41) == PressPhase::Ignored,
        "autorepeat keydown before release must be ignored");
    Require(tracker.Consume(0x841) == PressPhase::Ignored,
        "key release must not invoke command action");
    Require(tracker.Consume(0x41) == PressPhase::SecondPress,
        "new keydown after release must be eligible second press");
    Require(tracker.Consume(0x841) == PressPhase::Ignored,
        "second release must reset held state");
    Require(tracker.Consume(0x42) == PressPhase::FirstPress,
        "different bound key must begin a new sequence");
    Require(tracker.Consume(0x842) == PressPhase::Ignored,
        "release of the different key must only close its own press");
    Require(tracker.Consume(0x1042) == PressPhase::FirstPress,
        "different modifier binding must not count as same hotkey double press");
    tracker.Reset();
    Require(tracker.Consume(0x41) == PressPhase::FirstPress,
        "reset must discard previous sequence");
    tracker.Reset();
    Require(tracker.Consume(0x1041) == PressPhase::FirstPress &&
        tracker.Consume(0x841) == PressPhase::Ignored &&
        tracker.Consume(0x1041) == PressPhase::SecondPress,
        "release should match physical key even if modifiers changed before keyup");
}
