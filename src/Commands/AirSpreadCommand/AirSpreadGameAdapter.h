#pragma once

#include "Commands/AirSpreadCommand/IAirSpreadGamePort.h"

namespace ra_commands::commands
{
    class ClickedMissionDispatcher;
}

namespace ra_commands::game
{
    /** 仅在游戏线程使用；调度器由 Bootstrap 持有，必须比本适配器存活更久。 */
    class AirSpreadGameAdapter final : public air_spread::IAirSpreadGamePort
    {
    public:
        explicit AirSpreadGameAdapter(commands::ClickedMissionDispatcher& dispatcher);

        [[nodiscard]] bool TryCaptureSnapshot(
            air_spread::AirSpreadSnapshot& outSnapshot) const override;
        [[nodiscard]] bool SubmitMove(
            air_spread::ActorId actor, air_spread::Cell destination) override;

    private:
        commands::ClickedMissionDispatcher& mDispatcher;
    };
}
