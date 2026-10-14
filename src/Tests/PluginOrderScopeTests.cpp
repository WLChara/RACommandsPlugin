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
        const PluginOrderScope outer({3, 17});
        {
            const PluginOrderScope inner({5, 18});
            if (GetPluginOrderContext().mProducerTag != 5 || GetPluginOrderContext().mEpoch != 18)
            {
                throw std::runtime_error("nested native capture must use the inner producer and epoch");
            }
            if (!IsPluginOrderActive())
            {
                throw std::runtime_error("nested plugin orders must remain suppressed");
            }
        }
        if (!IsPluginOrderActive())
        {
            throw std::runtime_error("inner scope must not unsuppress an outer plugin order");
        }
        if (GetPluginOrderContext().mProducerTag != 3 || GetPluginOrderContext().mEpoch != 17)
        {
            throw std::runtime_error("inner scope must restore the outer native capture context");
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
    if (GetPluginOrderContext().mProducerTag != 0xFF || GetPluginOrderContext().mEpoch != 0)
    {
        throw std::runtime_error("native capture context leaked into a later human order");
    }
}
