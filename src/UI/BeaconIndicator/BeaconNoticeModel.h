#pragma once

#include "UI/Overlay/HudTypes.h"
#include <array>
#include <cstdint>
#include <vector>

namespace ra_commands::ui
{
    struct BeaconSlotSnapshot
    {
        bool mPresent = false;
        bool mEligible = false;
        HudWorldPoint mPosition;
        HudColor mColor;
    };

    /** 仅持有值快照；原生放置回调决定新代次，槽位复用不依赖指针变化。 */
    class BeaconNoticeModel
    {
    public:
        static constexpr std::size_t SLOT_COUNT = 24;
        static constexpr std::uint64_t LIFETIME_MS = 8000;
        static bool IsEligibleOwner(int owner, int localOwner, bool isDefeated,
            bool isLocalAllied, bool isOwnerAllied) noexcept;
        void Reset() noexcept;
        void OnPlaced(int owner, int slot, const BeaconSlotSnapshot& snapshot,
            std::uint64_t nowMs);
        void Reconcile(const std::array<BeaconSlotSnapshot, SLOT_COUNT>& slots,
            std::uint64_t nowMs) noexcept;
        std::vector<BeaconNotice> Capture(std::uint64_t nowMs) const;

    private:
        std::array<BeaconNotice, SLOT_COUNT> mNotices{};
        std::uint64_t mNextId = 1;
    };
}
