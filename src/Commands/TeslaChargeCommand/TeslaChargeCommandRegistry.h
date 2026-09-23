#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using TeslaChargeExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterTeslaChargeCommand(
        const GameSymbols& symbols,
        TeslaChargeExecuteCallback callback,
        std::string& outError);
    void DisableTeslaChargeCommand();
}
