#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using AutoCrushAddExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterAutoCrushAddCommand(
        const GameSymbols& symbols,
        AutoCrushAddExecuteCallback callback,
        std::string& outError);
    void DisableAutoCrushAddCommand();
}
