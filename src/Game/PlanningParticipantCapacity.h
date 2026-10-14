#pragma once
#include <string>

namespace ra_commands::game::planning_participant_capacity
{
    /** 只能由已经通过目标 EXE 哈希门禁的就绪游戏线程调用。
     * 仅移除本地规划参与者的128数量检查；不调整路径长度或模拟/网络数据。
     * 同线程可在 Disable 后重新启用。失败只影响本能力。
     */
    bool Install(std::string& outError);
    /** 任意线程逻辑停用；保留 Hook/跳板，后续执行原生数量检查。 */
    void Disable() noexcept;
    /** 任意线程读取逻辑启用状态，不表示当前会话满足放行门禁。 */
    bool IsEnabled() noexcept;
}
