#pragma once

#include "ClickedMission/ClickedMissionQueue.h"

namespace ra_commands::game
{
    class AutoFormationGameAdapter;
    /** Bootstrap 启动时绑定进程期实例；只在游戏线程调用，Reset 不销毁绑定实例。 */
    void BindAutoFormationGameAdapter(AutoFormationGameAdapter* adapter) noexcept;
    [[nodiscard]] bool ValidateAutoFormationIntent(const commands::ClickedMissionIntent& intent);
    void AttemptAutoFormationIntent(const commands::ClickedMissionIntent& intent);
}
