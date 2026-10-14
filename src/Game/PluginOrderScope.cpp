#include "Game/PluginOrderScope.h"

#include <cstdint>

namespace ra_commands::game
{
    namespace
    {
        thread_local std::uint32_t g_PluginOrderDepth = 0;
        thread_local PluginOrderContext g_PluginOrderContext{};
    }

    PluginOrderScope::PluginOrderScope(PluginOrderContext context) noexcept
        : mPrevious(g_PluginOrderContext)
    {
        g_PluginOrderContext = context;
        ++g_PluginOrderDepth;
    }

    PluginOrderScope::~PluginOrderScope()
    {
        --g_PluginOrderDepth;
        g_PluginOrderContext = mPrevious;
    }

    bool IsPluginOrderActive() noexcept
    {
        return g_PluginOrderDepth != 0;
    }

    PluginOrderContext GetPluginOrderContext() noexcept
    {
        return g_PluginOrderContext;
    }
}
