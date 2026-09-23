#pragma once

namespace ra_commands::game
{
    enum class CommandRegistrationResult
    {
        Pending,
        Registered,
        NameConflict,
        Failed
    };
}
