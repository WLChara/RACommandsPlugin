#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using AFloorExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterAFloorCommand(
        const GameSymbols& symbols,
        AFloorExecuteCallback callback,
        std::string& outError);
    void DisableAFloorCommand();
}
