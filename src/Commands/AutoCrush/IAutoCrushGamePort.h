#pragma once

#include "Commands/AutoCrush/AutoCrushPlanner.h"

#include <cstdint>
#include <vector>

namespace ra_commands::auto_crush
{
    /** 游戏主线程边界；服务只保存稳定 ID 与对局代次，不保存游戏对象指针。 */
    class IAutoCrushGamePort
    {
    public:
        virtual ~IAutoCrushGamePort() = default;

        [[nodiscard]] virtual bool IsSessionActive() const = 0;
        [[nodiscard]] virtual std::uint32_t Epoch() const = 0;
        [[nodiscard]] virtual std::uint32_t CurrentFrame() const = 0;
        // 仅筛选当前选中且仍可自动碾压的本地地面载具。
        [[nodiscard]] virtual std::vector<CrusherId> CaptureSelectedEligibleCrushers() const = 0;
        // 包含当前选中的本地 Unit，即使它已不符合自动碾压资格。
        [[nodiscard]] virtual std::vector<CrusherId> CaptureSelectedVehicleIds() const = 0;
        [[nodiscard]] virtual bool IsEligibleCrusher(CrusherId id) const = 0;
        // 每辆车只采集目标半径 8 格、落点最多 9 格的八方向走廊值快照。
        [[nodiscard]] virtual bool TryCaptureSnapshot(
            const std::vector<CrusherId>& ids, Snapshot& outSnapshot) const = 0;
        // true 仅表示共用调度队列接受意图，不保证原生事件已经入队。
        [[nodiscard]] virtual bool SubmitMove(CrusherId id, Cell destination) = 0;
        virtual void CancelPending(CrusherId id) = 0;
    };
}
