#pragma once

#include "ClickedMission/IClickedMissionGamePort.h"

namespace ra_commands::game
{
    /**
     * 共用的游戏 ClickedMission 接入层，独占帧、原生队列和对象身份验证。
     * 当前支持 Enter 和磁暴线圈充能 Attack；按任务逐项复验对象与即时条件。
     */
    class ClickedMissionGameAdapter final : public commands::IClickedMissionGamePort
    {
    public:
        [[nodiscard]] bool IsMatchReady() const override;
        [[nodiscard]] std::uintptr_t GetSessionIdentity() const override;
        [[nodiscard]] std::uint32_t GetCurrentFrame() const override;
        [[nodiscard]] std::uint32_t GetNativeFreeSlots() const override;
        [[nodiscard]] bool ValidateClickedMissionIntent(
            const commands::ClickedMissionIntent& intent) const override;
        void AttemptClickedMission(const commands::ClickedMissionIntent& intent) const override;
    };
}
