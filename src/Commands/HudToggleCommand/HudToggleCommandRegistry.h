#pragma once

#include "Commands/CommandRegistrationResult.h"
#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using HudToggleCallback = void(*)();

    CommandRegistrationResult TryRegisterBeaconIndicatorToggleCommand(
        const GameSymbols& symbols, HudToggleCallback callback, std::string& outError);
    CommandRegistrationResult TryRegisterRadarFeedToggleCommand(
        const GameSymbols& symbols, HudToggleCallback callback, std::string& outError);
    CommandRegistrationResult TryRegisterChatPanelToggleCommand(
        const GameSymbols& symbols, HudToggleCallback callback, std::string& outError);
    void DisableBeaconIndicatorToggleCommand();
    void DisableRadarFeedToggleCommand();
    void DisableChatPanelToggleCommand();
}
