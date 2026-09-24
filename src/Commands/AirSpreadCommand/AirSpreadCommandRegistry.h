#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using AirSpreadExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterAirSpreadCommand(
        const GameSymbols& symbols,
        AirSpreadExecuteCallback callback,
        std::string& outError);
    void DisableAirSpreadCommand();
}
