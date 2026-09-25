#pragma once

class TechnoClass;

namespace ra_commands::game
{
    [[nodiscard]] bool IsLocalTesla(TechnoClass* techno);
    [[nodiscard]] bool IsLocalTeslaCharger(TechnoClass* techno);
    [[nodiscard]] bool CanChargeTesla(TechnoClass* charger, TechnoClass* tesla);
}
