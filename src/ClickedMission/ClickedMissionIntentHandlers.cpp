#include "ClickedMission/ClickedMissionIntentHandlers.h"

namespace ra_commands::commands
{
    bool ClickedMissionIntentHandlers::Bind(ClickedMissionProducer producer,
        ClickedMissionIntentHandler handler) noexcept
    {
        const auto index = static_cast<std::size_t>(producer);
        if (producer == ClickedMissionProducer::Unspecified ||
            index >= mHandlers.size() || !handler.Validate || !handler.Attempt)
        {
            return false;
        }
        auto& existing = mHandlers[index];
        if (existing.Validate || existing.Attempt)
        {
            return existing == handler;
        }
        existing = handler;
        return true;
    }

    const ClickedMissionIntentHandler* ClickedMissionIntentHandlers::Find(
        ClickedMissionProducer producer) const noexcept
    {
        const auto index = static_cast<std::size_t>(producer);
        if (index >= mHandlers.size() || !mHandlers[index].Validate ||
            !mHandlers[index].Attempt)
        {
            return nullptr;
        }
        return &mHandlers[index];
    }
}
