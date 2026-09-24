#pragma once

#include <cstdint>
#include <vector>

namespace ra_commands::beacon_clear
{
    struct BeaconSlot
    {
        std::int32_t Owner = -1;
        std::int32_t Slot = -1;

        [[nodiscard]] bool operator==(const BeaconSlot&) const = default;
    };

    /** 仅传递信标槽的值；实现及服务都只在游戏主线程调用。 */
    class IBeaconClearGamePort
    {
    public:
        virtual ~IBeaconClearGamePort() = default;

        [[nodiscard]] virtual bool IsMatchReady() const = 0;
        [[nodiscard]] virtual std::uintptr_t GetSessionIdentity() const = 0;
        [[nodiscard]] virtual std::uint32_t GetCurrentFrame() const = 0;
        /** 非正值表示当前发送间隔无效，服务按 30 帧计算期限。 */
        [[nodiscard]] virtual std::int32_t GetFrameSendRate() const = 0;

        /** 返回当前已占用的 owner 0..7、slot 0..2；服务仍会校验并去重。 */
        [[nodiscard]] virtual std::vector<BeaconSlot> CaptureOccupiedSlots() const = 0;

        /**
         * 实现须复验会话和槽位；只有所有原生发送调用都返回非零，才具体删除本地槽。
         * false 不保证先前的部分发送已撤销；true 也不证明底层写入或远端处理成功。
         */
        [[nodiscard]] virtual bool TryBroadcastAndDelete(std::int32_t owner, std::int32_t slot) = 0;
    };
}
