#include "UI/BeaconIndicator/BeaconNoticeModel.h"

namespace ra_commands::ui
{
    bool BeaconNoticeModel::IsEligibleOwner(int owner, int localOwner,
        bool isDefeated, bool isLocalAllied, bool isOwnerAllied) noexcept
    {
        return owner >= 0 && owner < 8 && localOwner >= 0 && localOwner < 8 &&
            owner != localOwner && !isDefeated && isLocalAllied && isOwnerAllied;
    }

    void BeaconNoticeModel::Reset() noexcept
    {
        mNotices = {};
    }

    void BeaconNoticeModel::OnPlaced(int owner, int slot,
        const BeaconSlotSnapshot& snapshot, std::uint64_t nowMs)
    {
        if (owner < 0 || owner >= 8 || slot < 0 || slot >= 3) { return; }
        auto& notice = mNotices[static_cast<std::size_t>(owner * 3 + slot)];
        notice = {};
        if (!snapshot.mPresent || !snapshot.mEligible) { return; }
        // Reset 不回收 ID，避免旧画面中的点击或槽位复用命中新代次。
        if (!mNextId) { return; }
        notice = {mNextId++, owner, slot, snapshot.mPosition, snapshot.mColor,
            nowMs + LIFETIME_MS};
    }

    void BeaconNoticeModel::Reconcile(
        const std::array<BeaconSlotSnapshot, SLOT_COUNT>& slots,
        std::uint64_t nowMs) noexcept
    {
        for (std::size_t index = 0; index < SLOT_COUNT; ++index)
        {
            auto& notice = mNotices[index];
            const auto& slot = slots[index];
            if (!slot.mPresent || !slot.mEligible || nowMs >= notice.mExpiresAtMs ||
                notice.mPosition.mX != slot.mPosition.mX ||
                notice.mPosition.mY != slot.mPosition.mY ||
                notice.mPosition.mZ != slot.mPosition.mZ)
            {
                notice = {};
            }
            else { notice.mColor = slot.mColor; }
        }
    }

    std::vector<BeaconNotice> BeaconNoticeModel::Capture(std::uint64_t nowMs) const
    {
        std::vector<BeaconNotice> result;
        for (const auto& notice : mNotices)
        {
            if (notice.mId && nowMs < notice.mExpiresAtMs) { result.push_back(notice); }
        }
        return result;
    }
}
