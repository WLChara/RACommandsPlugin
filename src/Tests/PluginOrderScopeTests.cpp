#include "Game/PluginOrderScope.h"

#include <stdexcept>

void RunPluginOrderScopeTests()
{
    using namespace ra_commands::game;
    if (IsPluginOrderActive())
    {
        throw std::runtime_error("native order suppression leaked before the test");
    }
    {
        const PluginOrderScope outer;
        {
            const PluginOrderScope inner;
            if (!IsPluginOrderActive())
            {
                throw std::runtime_error("nested plugin orders must remain suppressed");
            }
        }
        if (!IsPluginOrderActive())
        {
            throw std::runtime_error("inner scope must not unsuppress an outer plugin order");
        }
    }
    try
    {
        const PluginOrderScope failingOrder;
        throw 1;
    }
    catch (int)
    {
    }
    if (IsPluginOrderActive())
    {
        throw std::runtime_error("exception must restore external order observation");
    }
}
