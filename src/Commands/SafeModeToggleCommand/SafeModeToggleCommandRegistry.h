#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using SafeModeToggleExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterSafeModeToggleCommand(
        const GameSymbols& symbols,
        SafeModeToggleExecuteCallback callback,
        std::string& outError);
    void DisableSafeModeToggleCommand();
}
