#pragma once

#include "Commands/AutoLoadCommand/AutoLoadPlanner.h"
#include "ClickedMission/ClickedMissionQueue.h"

#include <cstdint>

namespace ra_commands::autoload
{
    /**
     * 自动装车配对与游戏对象之间的边界，由 Game 适配器实现。
     * 所有方法只在已确认的游戏线程调用；接口不拥有游戏对象。
     */
    class IAutoLoadGamePort
    {
    public:
        virtual ~IAutoLoadGamePort() = default;

        /** 对局未就绪时返回 false，且保持 outSnapshot 不变。 */
        [[nodiscard]] virtual bool CaptureSnapshot(Snapshot& outSnapshot) const = 0;
        [[nodiscard]] virtual std::uint64_t GetCurrentTimeMs() const = 0;

        /** 为当前游戏对象捕获身份；目标失效时返回 false，且保持 outIntent 不变。 */
        [[nodiscard]] virtual bool MakeEnterIntent(
            UnitId passengerId,
            UnitId transportId,
            std::uint32_t epoch,
            commands::ClickedMissionIntent& outIntent) const = 0;
        virtual void Deselect(UnitId id) const = 0;
    };
}
