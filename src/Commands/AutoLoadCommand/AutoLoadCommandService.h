#pragma once

#include "Commands/AutoLoadCommand/IAutoLoadGamePort.h"
#include "ClickedMission/ClickedMissionDispatcher.h"

namespace ra_commands::autoload
{
    /**
     * 只负责将自动装车配对转成 Enter 意图，交给共用调度器。
     * 仅在游戏线程使用，不拥有游戏适配器或调度器。
     */
    class AutoLoadCommandService final
    {
    public:
        AutoLoadCommandService(IAutoLoadGamePort& game, commands::ClickedMissionDispatcher& dispatcher);

        // 入队成功后立即取消实际参与单位的选择；待发意图之后仍可能到期或失效。
        void OnHotkey();

    private:
        IAutoLoadGamePort& mGame;
        commands::ClickedMissionDispatcher& mDispatcher;
    };
}
