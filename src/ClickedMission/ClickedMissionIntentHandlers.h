#pragma once

#include "ClickedMission/ClickedMissionQueue.h"

#include <array>
#include <cstddef>

namespace ra_commands::commands
{
    struct ClickedMissionIntentHandler
    {
        bool(*Validate)(const ClickedMissionIntent&) = nullptr;
        // Drain 校验与实际调用之间状态可能变化；Attempt 必须再次校验。
        void(*Attempt)(const ClickedMissionIntent&) = nullptr;

        bool operator==(const ClickedMissionIntentHandler&) const = default;
    };

    /** 启动阶段绑定，游戏线程只读；不持有功能对象或游戏指针。 */
    class ClickedMissionIntentHandlers final
    {
    public:
        [[nodiscard]] bool Bind(ClickedMissionProducer producer,
            ClickedMissionIntentHandler handler) noexcept;
        [[nodiscard]] const ClickedMissionIntentHandler* Find(
            ClickedMissionProducer producer) const noexcept;

    private:
        std::array<ClickedMissionIntentHandler,
            static_cast<std::size_t>(ClickedMissionProducer::Count)> mHandlers{};
    };
}
