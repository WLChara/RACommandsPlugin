#include <Windows.h>
#include "Memory/ProcessMemory.h"

#include "VerifiedOperandPatch.h"

#include <cstring>
#include <limits>

namespace
{
    using ra_commands::memory::PatchResult;
    using ra_commands::memory::PatchSite;
    using ra_commands::memory::PatchState;

    bool ResolveAddress(
        const PatchSite& site,
        std::uintptr_t originalAnchor,
        std::uintptr_t resolvedAnchor,
        std::uintptr_t& outAddress)
    {
        if (site.mInstructionSize == 0 || site.mInstructionSize > site.mExpected.size()
            || static_cast<std::size_t>(site.mOperandOffset) + sizeof(std::uint32_t) > site.mInstructionSize
            || site.mInstructionAddress < originalAnchor)
        {
            return false;
        }

        const auto offset = site.mInstructionAddress - originalAnchor;
        if (resolvedAnchor == 0 || offset > (std::numeric_limits<std::uintptr_t>::max)() - resolvedAnchor)
        {
            return false;
        }

        outAddress = resolvedAnchor + offset;
        return true;
    }

    bool GetReplacementValue(
        const PatchSite& site,
        std::uintptr_t replacementBase,
        std::uint32_t& outValue)
    {
        if (site.mReplacementKind == ra_commands::memory::ReplacementKind::Immediate)
        {
            outValue = site.mReplacementValue;
            return true;
        }

        if (replacementBase > (std::numeric_limits<std::uint32_t>::max)()
            || site.mReplacementValue > (std::numeric_limits<std::uint32_t>::max)() - replacementBase)
        {
            return false;
        }

        outValue = static_cast<std::uint32_t>(replacementBase + site.mReplacementValue);
        return true;
    }

    bool MatchesValue(const PatchSite& site, std::uintptr_t address, std::uint32_t value)
    {
        std::array<std::uint8_t, 7> actual{};
        if (!ra_commands::memory::TryReadMemory(address, actual.data(), site.mInstructionSize))
            return false;
        const auto* bytes = actual.data();
        for (std::size_t i = 0; i < site.mInstructionSize; ++i)
        {
            if ((bytes[i] & site.mMask[i]) != (site.mExpected[i] & site.mMask[i]))
            {
                return false;
            }
        }

        std::uint32_t operand = 0;
        std::memcpy(&operand, bytes + site.mOperandOffset, sizeof(operand));
        return operand == value;
    }

    bool WriteOperand(std::uintptr_t address, std::uint32_t value)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(value), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            return false;
        }

        std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));

        std::uint32_t writtenValue = 0;
        std::memcpy(&writtenValue, reinterpret_cast<const void*>(address), sizeof(writtenValue));

        DWORD ignoredProtect = 0;
        const bool restored = VirtualProtect(
            reinterpret_cast<void*>(address), sizeof(value), oldProtect, &ignoredProtect) != FALSE;
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), sizeof(value));
        return restored && writtenValue == value;
    }

    PatchResult VerifySites(
        const PatchSite* sites,
        std::size_t count,
        std::uintptr_t originalAnchor,
        std::uintptr_t resolvedAnchor,
        std::uintptr_t replacementBase,
        PatchState state)
    {
        if (!sites || count == 0)
        {
            return PatchResult::InvalidSite;
        }

        // 先校验整组指令，避免版本不匹配时只修改了前半组。
        for (std::size_t i = 0; i < count; ++i)
        {
            std::uintptr_t address = 0;
            std::uint32_t replacement = 0;
            if (!ResolveAddress(sites[i], originalAnchor, resolvedAnchor, address)
                || !GetReplacementValue(sites[i], replacementBase, replacement))
            {
                return PatchResult::InvalidSite;
            }
            if (!MatchesValue(sites[i], address,
                    state == PatchState::Replacement ? replacement : sites[i].mOriginalValue))
            {
                return PatchResult::UnexpectedBytes;
            }
        }

        return PatchResult::Success;
    }

    PatchResult RunPatchSites(
        const PatchSite* sites,
        std::size_t count,
        std::uintptr_t originalAnchor,
        std::uintptr_t resolvedAnchor,
        std::uintptr_t replacementBase,
        bool restoring)
    {
        const auto verification = VerifySites(
            sites, count, originalAnchor, resolvedAnchor, replacementBase,
            restoring ? PatchState::Replacement : PatchState::Original);
        if (verification != PatchResult::Success)
        {
            return verification;
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            std::uintptr_t address = 0;
            std::uint32_t replacement = 0;
            ResolveAddress(sites[i], originalAnchor, resolvedAnchor, address);
            GetReplacementValue(sites[i], replacementBase, replacement);
            if (WriteOperand(address + sites[i].mOperandOffset,
                    restoring ? sites[i].mOriginalValue : replacement))
            {
                continue;
            }

            bool rolledBack = true;
            for (std::size_t rollback = 0; rollback <= i; ++rollback)
            {
                std::uintptr_t previousAddress = 0;
                std::uint32_t previousReplacement = 0;
                ResolveAddress(sites[rollback], originalAnchor, resolvedAnchor, previousAddress);
                GetReplacementValue(sites[rollback], replacementBase, previousReplacement);
                rolledBack = WriteOperand(
                    previousAddress + sites[rollback].mOperandOffset,
                    restoring ? previousReplacement : sites[rollback].mOriginalValue)
                    && rolledBack;
            }
            return rolledBack ? PatchResult::WriteFailed : PatchResult::RollbackFailed;
        }

        return PatchResult::Success;
    }

    bool HasCompatibleLayout(const PatchSite& currentSite, const PatchSite& replacementSite)
    {
        return currentSite.mInstructionAddress == replacementSite.mInstructionAddress
            && currentSite.mInstructionSize == replacementSite.mInstructionSize
            && currentSite.mOperandOffset == replacementSite.mOperandOffset
            && currentSite.mExpected == replacementSite.mExpected
            && currentSite.mMask == replacementSite.mMask
            && currentSite.mOriginalValue == replacementSite.mOriginalValue
            && currentSite.mReplacementKind == replacementSite.mReplacementKind;
    }
}

namespace ra_commands::memory
{
    PatchResult VerifyPatchSites(
        const PatchSite* sites,
        std::size_t count,
        std::uintptr_t originalAnchor,
        std::uintptr_t resolvedAnchor,
        std::uintptr_t replacementBase,
        PatchState state)
    {
        return VerifySites(
            sites, count, originalAnchor, resolvedAnchor, replacementBase, state);
    }

    PatchResult ApplyPatchSites(
        const PatchSite* sites,
        std::size_t count,
        std::uintptr_t originalAnchor,
        std::uintptr_t resolvedAnchor,
        std::uintptr_t replacementBase)
    {
        return RunPatchSites(
            sites, count, originalAnchor, resolvedAnchor, replacementBase, false);
    }

    PatchResult UpdatePatchSites(
        const PatchSite* currentSites,
        const PatchSite* replacementSites,
        std::size_t count,
        std::uintptr_t originalAnchor,
        std::uintptr_t resolvedAnchor,
        std::uintptr_t currentBase,
        std::uintptr_t replacementBase)
    {
        if (!currentSites || !replacementSites || count == 0)
        {
            return PatchResult::InvalidSite;
        }

        const auto verification = VerifySites(
            currentSites, count, originalAnchor, resolvedAnchor, currentBase, PatchState::Replacement);
        if (verification != PatchResult::Success)
        {
            return verification;
        }

        // 完成全部布局和值校验后才写入，避免后续站点失败时留下半组新值。
        for (std::size_t i = 0; i < count; ++i)
        {
            std::uintptr_t address = 0;
            std::uint32_t replacement = 0;
            if (!HasCompatibleLayout(currentSites[i], replacementSites[i])
                || !ResolveAddress(replacementSites[i], originalAnchor, resolvedAnchor, address)
                || !GetReplacementValue(replacementSites[i], replacementBase, replacement))
            {
                return PatchResult::InvalidSite;
            }
        }

        for (std::size_t i = 0; i < count; ++i)
        {
            std::uintptr_t address = 0;
            std::uint32_t replacement = 0;
            ResolveAddress(replacementSites[i], originalAnchor, resolvedAnchor, address);
            GetReplacementValue(replacementSites[i], replacementBase, replacement);
            if (WriteOperand(address + replacementSites[i].mOperandOffset, replacement))
            {
                continue;
            }

            bool rolledBack = true;
            for (std::size_t rollback = 0; rollback <= i; ++rollback)
            {
                std::uintptr_t previousAddress = 0;
                std::uint32_t previousReplacement = 0;
                ResolveAddress(currentSites[rollback], originalAnchor, resolvedAnchor, previousAddress);
                GetReplacementValue(currentSites[rollback], currentBase, previousReplacement);
                rolledBack = WriteOperand(
                    previousAddress + currentSites[rollback].mOperandOffset, previousReplacement)
                    && rolledBack;
            }
            return rolledBack ? PatchResult::WriteFailed : PatchResult::RollbackFailed;
        }

        return PatchResult::Success;
    }

    PatchResult RestorePatchSites(
        const PatchSite* sites,
        std::size_t count,
        std::uintptr_t originalAnchor,
        std::uintptr_t resolvedAnchor,
        std::uintptr_t replacementBase)
    {
        return RunPatchSites(
            sites, count, originalAnchor, resolvedAnchor, replacementBase, true);
    }

    const char* PatchResultText(PatchResult result)
    {
        switch (result)
        {
        case PatchResult::Success:
            return "success";
        case PatchResult::InvalidSite:
            return "invalid patch site or address";
        case PatchResult::UnexpectedBytes:
            return "instruction bytes do not match";
        case PatchResult::WriteFailed:
            return "operand write failed; prior values restored";
        case PatchResult::RollbackFailed:
            return "operand write failed; rollback was incomplete";
        }
        return "unknown patch result";
    }
}
