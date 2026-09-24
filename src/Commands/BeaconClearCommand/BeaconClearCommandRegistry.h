#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using BeaconClearExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterBeaconClearCommand(
        const GameSymbols& symbols,
        BeaconClearExecuteCallback callback,
        std::string& outError);
    void DisableBeaconClearCommand();
}
