#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using AutoBuildExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterMainAutoBuildCommand(
        const GameSymbols& symbols, AutoBuildExecuteCallback callback,
        std::string& outError);
    CommandRegistrationResult TryRegisterDefenseAutoBuildCommand(
        const GameSymbols& symbols, AutoBuildExecuteCallback callback,
        std::string& outError);
    void DisableMainAutoBuildCommand();
    void DisableDefenseAutoBuildCommand();
}
