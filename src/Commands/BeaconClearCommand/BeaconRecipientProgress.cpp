#include "Commands/BeaconClearCommand/BeaconRecipientProgress.h"

namespace ra_commands::beacon_clear
{
    bool BeaconRecipientProgress::Begin(
        std::size_t flatIndex, std::uintptr_t beaconAddress)
    {
        if (flatIndex >= SLOT_COUNT || beaconAddress == 0)
        {
            return false;
        }
        auto& previous = mBeaconAddresses[flatIndex];
        if (previous != 0 && previous != beaconAddress)
        {
            ClearSlot(flatIndex);
            return false;
        }
        previous = beaconAddress;
        return true;
    }

    bool BeaconRecipientProgress::HasSubmitted(
        std::size_t flatIndex, std::uintptr_t recipientAddress) const
    {
        return flatIndex < SLOT_COUNT &&
            mSubmittedRecipients[flatIndex].contains(recipientAddress);
    }

    void BeaconRecipientProgress::MarkSubmitted(
        std::size_t flatIndex, std::uintptr_t recipientAddress)
    {
        if (flatIndex < SLOT_COUNT && recipientAddress != 0)
        {
            mSubmittedRecipients[flatIndex].insert(recipientAddress);
        }
    }

    void BeaconRecipientProgress::ClearSlot(std::size_t flatIndex) noexcept
    {
        if (flatIndex < SLOT_COUNT)
        {
            mSubmittedRecipients[flatIndex].clear();
            mBeaconAddresses[flatIndex] = 0;
        }
    }

    void BeaconRecipientProgress::Reset() noexcept
    {
        for (std::size_t index = 0; index < SLOT_COUNT; ++index)
        {
            ClearSlot(index);
        }
    }
}
