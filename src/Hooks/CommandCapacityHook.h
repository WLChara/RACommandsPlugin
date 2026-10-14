#pragma once

#include <string>

namespace ra_commands::game::command_capacity::hooks
{
    [[nodiscard]] bool Install(std::string& outError);
    [[nodiscard]] bool IsInstalled() noexcept;
    [[nodiscard]] bool IsPlanningInstalled() noexcept;
}
