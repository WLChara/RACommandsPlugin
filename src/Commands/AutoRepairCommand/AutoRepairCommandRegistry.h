#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using AutoRepairExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterAutoRepairCommand(
        const GameSymbols& symbols,
        AutoRepairExecuteCallback callback,
        std::string& outError);
    void DisableAutoRepairCommand();
}
