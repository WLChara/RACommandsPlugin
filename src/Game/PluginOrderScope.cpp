#include "Game/PluginOrderScope.h"

#include <cstdint>

namespace ra_commands::game
{
    namespace
    {
        thread_local std::uint32_t g_PluginOrderDepth = 0;
    }

    PluginOrderScope::PluginOrderScope() noexcept
    {
        ++g_PluginOrderDepth;
    }

    PluginOrderScope::~PluginOrderScope()
    {
        --g_PluginOrderDepth;
    }

    bool IsPluginOrderActive() noexcept
    {
        return g_PluginOrderDepth != 0;
    }
}
