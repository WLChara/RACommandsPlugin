#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;

    CommandRegistrationResult TryRegisterIfvModeSelectCommand(
        const GameSymbols& symbols,
        void(*callback)(bool secondPress),
        std::string& outError);
    void DisableIfvModeSelectCommand();
}
