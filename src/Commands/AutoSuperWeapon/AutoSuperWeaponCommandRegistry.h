#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using AutoSuperWeaponExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterAutoIronCurtainCommand(
        const GameSymbols& symbols, AutoSuperWeaponExecuteCallback callback,
        std::string& outError);
    CommandRegistrationResult TryRegisterAutoRageInductorCommand(
        const GameSymbols& symbols, AutoSuperWeaponExecuteCallback callback,
        std::string& outError);
    void DisableAutoIronCurtainCommand();
    void DisableAutoRageInductorCommand();
}
