#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ra_commands::memory
{
    enum class ReplacementKind
    {
        Immediate,
        BaseRelativeAddress
    };

    // 描述一条指令中可替换的 32 位操作数；地址和字节模式由调用方提供。
    struct PatchSite
    {
        std::uintptr_t mInstructionAddress;
        std::array<std::uint8_t, 7> mExpected{};
        std::array<std::uint8_t, 7> mMask{};
        std::uint8_t mInstructionSize;
        std::uint8_t mOperandOffset;
        ReplacementKind mReplacementKind;
        std::uint32_t mOriginalValue;
        std::uint32_t mReplacementValue;
    };

    enum class PatchResult
    {
        Success,
        InvalidSite,
        UnexpectedBytes,
        WriteFailed,
        RollbackFailed
    };

    enum class PatchState
    {
        Original,
        Replacement
    };

    // 只校验整组指令及操作数，不修改内存。
    PatchResult VerifyPatchSites(
        const PatchSite* sites,
        std::size_t count,
        std::uintptr_t originalAnchor,
        std::uintptr_t resolvedAnchor,
        std::uintptr_t replacementBase,
        PatchState state);

    // 在全部站点通过原始字节校验后写入；写入失败时回滚已处理站点。
    // originalAnchor/resolvedAnchor 用于同一映像版本的地址平移。
    PatchResult ApplyPatchSites(
        const PatchSite* sites,
        std::size_t count,
        std::uintptr_t originalAnchor,
        std::uintptr_t resolvedAnchor,
        std::uintptr_t replacementBase);

    /**
     * 在当前整组站点仍匹配旧替换值时，直接改写为新替换值。
     * 两组描述必须具有相同的指令布局与原始值，仅替换值和基址可变。
     * 调用方须保证调用期间无并发修改，并在活动读取者安全的时机调用；
     * 写入失败时尝试将已处理站点（包括失败站点）恢复为旧替换值。
     */
    PatchResult UpdatePatchSites(
        const PatchSite* currentSites,
        const PatchSite* replacementSites,
        std::size_t count,
        std::uintptr_t originalAnchor,
        std::uintptr_t resolvedAnchor,
        std::uintptr_t currentBase,
        std::uintptr_t replacementBase);

    // 仅在全部站点仍匹配替换值时还原；失败时尝试回滚到替换状态。
    PatchResult RestorePatchSites(
        const PatchSite* sites,
        std::size_t count,
        std::uintptr_t originalAnchor,
        std::uintptr_t resolvedAnchor,
        std::uintptr_t replacementBase);

    const char* PatchResultText(PatchResult result);
}
