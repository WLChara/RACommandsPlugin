#pragma once

#include "Commands/AutoLoadCommand/IAutoLoadGamePort.h"

#include <cstdint>

namespace ra_commands::game
{
    /**
     * 将 YRpp 游戏对象转换为装车快照及 Enter 意图。
     * 不持有游戏对象；跨帧只传递可重新验证的身份值。
     */
    class AutoLoadGameAdapter final : public autoload::IAutoLoadGamePort
    {
    public:
        [[nodiscard]] bool CaptureSnapshot(autoload::Snapshot& outSnapshot) const override;
        [[nodiscard]] bool MakeEnterIntent(
            autoload::UnitId passengerId,
            autoload::UnitId transportId,
            std::uint32_t epoch,
            commands::ClickedMissionIntent& outIntent) const override;
        void Deselect(autoload::UnitId id) const override;
    };
}
