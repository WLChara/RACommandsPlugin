#pragma once

#include "Commands/BeaconClearCommand/IBeaconClearGamePort.h"
#include "Commands/BeaconClearCommand/BeaconRecipientProgress.h"

namespace ra_commands::game
{
    /** 封装目标样本的信标槽布局与原生 Global Channel 删除命令。 */
    class BeaconClearGameAdapter final : public beacon_clear::IBeaconClearGamePort
    {
    public:
        [[nodiscard]] bool IsMatchReady() const override;
        [[nodiscard]] std::uintptr_t GetSessionIdentity() const override;
        [[nodiscard]] std::uint32_t GetCurrentFrame() const override;
        [[nodiscard]] std::int32_t GetFrameSendRate() const override;
        [[nodiscard]] std::vector<beacon_clear::BeaconSlot> CaptureOccupiedSlots() const override;
        [[nodiscard]] bool TryBroadcastAndDelete(
            std::int32_t owner, std::int32_t slot) override;
        void Reset() noexcept;

    private:
        void SyncSessionState();

        beacon_clear::BeaconRecipientProgress mProgress;
        std::uintptr_t mSessionIdentity = 0;
        std::uint32_t mLastFrame = 0;
    };
}
