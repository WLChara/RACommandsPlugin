#include "Game/SelectionCapacity.h"
#include "Game/GameObjectAccess.h"
#include "Game/TargetProfiles/SelectionCapacityProfile.h"
#include "Hooks/SelectionCapacityHooks.h"
#include "Memory/ProcessMemory.h"
#include "SelectionCapacity/CandidateStorage.h"
#include <Windows.h>
#include <HouseClass.h>
#include <SessionClass.h>
#include <TacticalClass.h>
#include <atomic>
#include <cstring>

namespace ra_commands::game::selection_capacity
{
    namespace
    {
        using ra_commands::selection_capacity::CandidateEntry;
        using ra_commands::selection_capacity::CandidateStorage;
        using PublishResult = CandidateStorage::PublishResult;

        struct StateOwner
        {
            CandidateEntry mFixed[profile::FIXED_CAPACITY]{};
            CandidateStorage mDynamic;
            CandidateEntry* mActive = mFixed;
            std::size_t mCapacity = profile::FIXED_CAPACITY;
            profile::PatchSites mSites = profile::FIXED_PATCH_SITES;
            DWORD mThread = 0;
            unsigned mReaderDepth = 0;
            unsigned mWriterDepth = 0;
            bool mInstalled = false;
            bool mHooksReady = false;
            bool mLocalMode = false;
            bool mEpochValid = true;
            bool mPatchUncertain = false;
            bool mSessionActive = false;
            bool mFrameSeen = false;
            bool mResetPending = false;
            std::uint32_t mWorldEpoch = 0;
            std::atomic<bool> mDisabled{false};
        };
        // DLL 已由 Bootstrap 固定驻留；所有原生引用随进程存活。
        StateOwner g_State;

        bool IsOwnerThread() { return g_State.mThread == GetCurrentThreadId(); }
        int& Count(void* tactical)
        {
            return *reinterpret_cast<int*>(static_cast<std::uint8_t*>(tactical) + profile::COUNT_OFFSET);
        }
        void InvalidateEpoch(void* tactical)
        {
            g_State.mEpochValid = false;
            if (tactical) Count(tactical) = 0;
        }
        bool WantsDynamicStorage()
        {
            const auto mode = SessionClass::Instance->GameMode;
            std::uint32_t replayFlags = profile::REPLAY_FLAG;
            return g_State.mHooksReady && !g_State.mDisabled.load(std::memory_order_acquire)
                && g_State.mSessionActive && HouseClass::Player.get()
                && (mode == GameMode::Campaign || mode == GameMode::Skirmish)
                && memory::TryReadMemory(profile::REPLAY_FLAGS_ADDRESS, &replayFlags, sizeof(replayFlags))
                && (replayFlags & profile::REPLAY_FLAG) == 0;
        }
        profile::PatchSites MakeSites(std::size_t capacity)
        {
            auto sites = profile::FIXED_PATCH_SITES;
            sites[0].mReplacementValue = static_cast<std::uint32_t>(capacity);
            sites[1].mReplacementValue = static_cast<std::uint32_t>(capacity);
            sites[11].mReplacementValue = static_cast<std::uint32_t>(capacity * sizeof(CandidateEntry));
            return sites;
        }
        PublishResult BindStorage(CandidateEntry* buffer, std::size_t capacity)
        {
            if (g_State.mPatchUncertain || g_State.mReaderDepth) return PublishResult::Rejected;
            const auto replacement = MakeSites(capacity);
            const auto result = memory::UpdatePatchSites(g_State.mSites.data(), replacement.data(),
                replacement.size(), profile::BUILD_ADDRESS, profile::BUILD_ADDRESS,
                reinterpret_cast<std::uintptr_t>(g_State.mActive), reinterpret_cast<std::uintptr_t>(buffer));
            if (result != memory::PatchResult::Success) {
                if (result == memory::PatchResult::RollbackFailed) {
                    g_State.mPatchUncertain = true;
                    g_State.mEpochValid = false;
                    return PublishResult::Uncertain;
                }
                return PublishResult::Rejected;
            }
            g_State.mSites = replacement;
            g_State.mActive = buffer;
            g_State.mCapacity = capacity;
            return PublishResult::Completed;
        }
        PublishResult PublishGrowth(void*, CandidateEntry* buffer, std::size_t capacity)
        { return BindStorage(buffer, capacity); }

        void ResetWorld(void* tactical)
        {
            g_State.mResetPending = true;
            InvalidateEpoch(tactical);
            g_State.mLocalMode = false;
            if (g_State.mReaderDepth || g_State.mWriterDepth || g_State.mPatchUncertain) return;
            if (g_State.mActive != g_State.mFixed
                && BindStorage(g_State.mFixed, profile::FIXED_CAPACITY) != PublishResult::Completed) return;
            g_State.mDynamic.Reset();
            std::memset(g_State.mFixed, 0, sizeof(g_State.mFixed));
            g_State.mResetPending = false;
        }

        void OnEpoch(void* tactical)
        {
            if (!IsOwnerThread()) return;
            // trampoline 紧接着清 count；此处尚无缓存候选表地址的 reader/writer。
            if (g_State.mPatchUncertain || g_State.mReaderDepth || g_State.mWriterDepth) {
                InvalidateEpoch(tactical);
                return;
            }
            // 在 reader 内收到换局通知时延期释放；下一个安全 epoch 完成重置。
            if (g_State.mResetPending) {
                ResetWorld(tactical);
                if (g_State.mResetPending) return;
            }
            const bool localMode = WantsDynamicStorage();
            if (!localMode && g_State.mActive != g_State.mFixed) {
                ResetWorld(tactical);
                if (g_State.mActive != g_State.mFixed || g_State.mPatchUncertain) return;
            }
            g_State.mLocalMode = localMode;
            g_State.mEpochValid = true;
        }

        bool EnsureRoom(void* tactical)
        {
            if (!IsOwnerThread() || !g_State.mEpochValid || g_State.mPatchUncertain) return false;
            const int count = Count(tactical);
            if (count < 0 || static_cast<std::size_t>(count) > g_State.mCapacity) {
                InvalidateEpoch(tactical);
                return false;
            }
            if (static_cast<std::size_t>(count) < g_State.mCapacity) return true;
            if (!g_State.mLocalMode || g_State.mDisabled.load(std::memory_order_acquire)) return false;
            if (g_State.mReaderDepth || !g_State.mDynamic.TryReserve(
                    static_cast<std::size_t>(count) + 1, g_State.mActive,
                    static_cast<std::size_t>(count), g_State.mCapacity, PublishGrowth, nullptr)) {
                InvalidateEpoch(tactical);
                return false;
            }
            return true;
        }

        bool EnsureCandidateRoom(void* tactical, int x, int y)
        {
            const auto width = *reinterpret_cast<const int*>(profile::VIEW_WIDTH_ADDRESS);
            const auto height = *reinterpret_cast<const int*>(profile::VIEW_HEIGHT_ADDRESS);
            // 用宽整数避免损坏的视窗尺寸在边界加法中溢出。
            return x >= -32 && y >= -32 && static_cast<std::int64_t>(x) <= static_cast<std::int64_t>(width) + 32
                && static_cast<std::int64_t>(y) <= static_cast<std::int64_t>(height) + 32 && EnsureRoom(tactical);
        }
        bool BeginWriter()
        {
            if (!IsOwnerThread()) return false;
            ++g_State.mWriterDepth;
            return true;
        }
        void EndWriter() { --g_State.mWriterDepth; }
        bool BeginReader(void* tactical)
        {
            if (!IsOwnerThread() || !g_State.mEpochValid || g_State.mPatchUncertain) return false;
            const int count = Count(tactical);
            if (count < 0 || static_cast<std::size_t>(count) > g_State.mCapacity) {
                InvalidateEpoch(tactical);
                return false;
            }
            ++g_State.mReaderDepth;
            return true;
        }
        void EndReader() { --g_State.mReaderDepth; }
    }

    bool Install(std::string& outError)
    {
        if (g_State.mInstalled) {
            if (!IsOwnerThread()) { outError = "selection capacity belongs to another game thread"; return false; }
            g_State.mDisabled.store(false, std::memory_order_release);
            g_State.mLocalMode = WantsDynamicStorage();
            outError.clear();
            return true;
        }
        if (g_State.mPatchUncertain || !IsGameSessionReady()) {
            outError = "selection capacity installation requires a ready session and consistent operands";
            return false;
        }
        auto* const tactical = TacticalClass::Instance.get();
        if (!tactical) { outError = "Tactical instance unavailable"; return false; }
        g_State.mThread = GetCurrentThreadId();
        g_State.mDisabled.store(false, std::memory_order_release);
        g_State.mSessionActive = true;
        // 中途注入时复制原表的有效候选，避免将旧 count 指向空表；不触碰 CurrentObjects。
        const int count = Count(tactical);
        const bool copied = count >= 0 && count <= 500 && (count == 0 || memory::TryReadMemory(profile::ORIGINAL_BASE,
            g_State.mFixed, static_cast<std::size_t>(count) * sizeof(CandidateEntry)));
        const auto applied = memory::ApplyPatchSites(profile::FIXED_PATCH_SITES.data(),
            profile::FIXED_PATCH_SITES.size(), profile::BUILD_ADDRESS, profile::BUILD_ADDRESS,
            reinterpret_cast<std::uintptr_t>(g_State.mFixed));
        if (applied != memory::PatchResult::Success) {
            if (applied == memory::PatchResult::RollbackFailed) g_State.mPatchUncertain = true;
            outError = memory::PatchResultText(applied);
            return false;
        }
        if (!copied) InvalidateEpoch(tactical);
        g_State.mInstalled = true;
        const hooks::selection_capacity::Callbacks callbacks = {
            &OnEpoch, &EnsureCandidateRoom, &BeginWriter, &EndWriter,
            &EnsureCandidateRoom, &BeginReader, &EndReader
        };
        g_State.mHooksReady = hooks::selection_capacity::Install(callbacks, outError);
        g_State.mLocalMode = WantsDynamicStorage();
        return true;
    }

    void OnGameFrame(bool sessionActive, std::uint32_t epoch)
    {
        if (!g_State.mInstalled || !IsOwnerThread()) return;
        if ((!g_State.mFrameSeen && !sessionActive) || (g_State.mFrameSeen
                && (sessionActive != g_State.mSessionActive || epoch != g_State.mWorldEpoch)))
            ResetWorld(TacticalClass::Instance.get());
        g_State.mSessionActive = sessionActive;
        g_State.mWorldEpoch = epoch;
        g_State.mFrameSeen = true;
    }

    State GetState()
    {
        return {g_State.mInstalled, g_State.mHooksReady && !g_State.mPatchUncertain
            && !g_State.mDisabled.load(std::memory_order_acquire), g_State.mCapacity};
    }

    void Disable() noexcept { g_State.mDisabled.store(true, std::memory_order_release); }
}
