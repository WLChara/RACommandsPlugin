#pragma once

#include "ClickedMission/IClickedMissionGamePort.h"
#include "ClickedMission/ClickedMissionIntentHandlers.h"

namespace ra_commands::game
{
    /**
     * 共用的游戏边界：提供对局/队列信息，并分派到启动时绑定的功能处理器。
     * 原生 Enter 留在此处，其余已绑定意图由功能处理器负责发送前校验。
     */
    class ClickedMissionGameAdapter final : public commands::IClickedMissionGamePort
    {
    public:
        /** 仅由 Bootstrap 在主帧回调启用前绑定；同一处理器可重复绑定。 */
        [[nodiscard]] bool BindIntentHandler(commands::ClickedMissionProducer producer,
            commands::ClickedMissionIntentHandler handler) noexcept;
        [[nodiscard]] bool IsMatchReady() const override;
        [[nodiscard]] std::uintptr_t GetSessionIdentity() const override;
        [[nodiscard]] std::uint32_t GetCurrentFrame() const override;
        [[nodiscard]] std::uint32_t GetNativeFreeSlots() const override;
        [[nodiscard]] bool ValidateClickedMissionIntent(
            const commands::ClickedMissionIntent& intent) const override;
        void AttemptClickedMission(const commands::ClickedMissionIntent& intent) const override;

    private:
        commands::ClickedMissionIntentHandlers mHandlers;
    };
}
