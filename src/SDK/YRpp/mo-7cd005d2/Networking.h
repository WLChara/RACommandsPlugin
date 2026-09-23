#pragma once

#include <YRPPCore.h>
#include <Helpers/CompileTime.h>

// Only the game globals used by this plugin are retained in this SDK view.
class Networking
{
public:
    // Number of events currently waiting in the game's outgoing queue.
    static constexpr reference<int, 0xA802C8u> const LastEventIndex{};

    // Interval used to schedule networked game frames.
    static constexpr reference<int, 0xA8B554u> const FrameSendRate{};
};
