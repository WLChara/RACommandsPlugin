#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;

    CommandRegistrationResult TryRegisterMindControlSelectCommand(
        const GameSymbols& symbols,
        void(*callback)(),
        std::string& outError);
    void DisableMindControlSelectCommand();
}
