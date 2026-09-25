#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using AutoCrushRemoveExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterAutoCrushRemoveCommand(
        const GameSymbols& symbols,
        AutoCrushRemoveExecuteCallback callback,
        std::string& outError);
    void DisableAutoCrushRemoveCommand();
}
