#pragma once

#include "ClickedMission/ClickedMissionQueue.h"

#include <cstdint>

namespace ra_commands::commands
{
    /**
     * 所有 ClickedMission 功能共用的游戏线程边界。
     * 实现者负责对每种已支持 Mission 的对象身份及即时条件进行验证。
     */
    class IClickedMissionGamePort
    {
    public:
        virtual ~IClickedMissionGamePort() = default;

        [[nodiscard]] virtual bool IsMatchReady() const = 0;
        // 返回不透明的对局身份；结合帧计数回退判定旧命令是否必须清除。
        [[nodiscard]] virtual std::uintptr_t GetSessionIdentity() const = 0;
        [[nodiscard]] virtual std::uint32_t GetCurrentFrame() const = 0;
        // 返回当前提交后端的真实可接收余量；原生计数无效时返回 0。
        [[nodiscard]] virtual std::uint32_t GetNativeFreeSlots() const = 0;
        [[nodiscard]] virtual bool ValidateClickedMissionIntent(
            const ClickedMissionIntent& intent) const = 0;
        // 仅尝试调用游戏接口；ClickedMission 的返回值不能证明原生 OutList 已入队。
        virtual void AttemptClickedMission(const ClickedMissionIntent& intent) const = 0;
        // 已通过游戏入口、但仍在传输后端等待的意图也属于可取消的待发阶段。
        virtual std::size_t CancelSubmittedByProducer(ClickedMissionProducer) const { return 0; }
        virtual std::size_t CancelSubmittedByProducerAndActor(
            ClickedMissionProducer, const ClickedMissionIdentity&) const { return 0; }
        [[nodiscard]] virtual bool HasSubmittedActor(ClickedMissionProducer,
            std::uintptr_t, std::uint32_t, std::uint32_t) const { return false; }
    };
}
