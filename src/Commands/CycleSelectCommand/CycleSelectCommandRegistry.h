#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;

    CommandRegistrationResult TryRegisterCycleSelectCommand(
        const GameSymbols& symbols,
        void(*callback)(),
        std::string& outError);
    void DisableCycleSelectCommand();
}
