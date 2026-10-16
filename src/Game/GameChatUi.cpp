#include "Game/GameChatUi.h"
#include "Game/GameHudUi.h"
#include "Game/ChatCommandGameAdapter.h"
#include "Game/GameChatSpeedAdapter.h"
#include "Game/TargetProfiles/ChatUiProfile.h"
#include "Game/TargetProfiles/ChatCommandProfile.h"
#include "Game/TargetVersion.h"
#include "Memory/ProcessMemory.h"
#include "UI/Chat/ChatState.h"
#include "UI/Chat/ImePositionSchedule.h"

#include <MinHook.h>
#include <imm.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <utility>

namespace ra_commands::game
{
    namespace
    {
        namespace profile = chat_profile;
        using ProcessInput = void(__thiscall*)(DWORD* input);
        using PrintMessage = int(__thiscall*)(void*, const wchar_t*, int, const wchar_t*, int, int, int, char);
        using ConcatMessage = int(__thiscall*)(void*, const wchar_t*, int, const wchar_t*, int);
        using AddEdit = void*(__thiscall*)(void*, int, int, const wchar_t*, char, int);
        using RemoveEdit = void(__thiscall*)(void*);
        using TranslateKey = char(__thiscall*)(void*, std::uint16_t);
        using FindBeacon = void*(__thiscall*)(void*, int);
        using TextWidth = int(__thiscall*)(void*, const wchar_t*, int);

        // 私有目标布局只用于已核对版本的适配；字段和原生指针不进入聊天模型。
        struct NativeMessageList
        {
            std::uint32_t mMessages;
            int mMessageX, mMessageY, mMaxMessages, mMaxCharacters, mHeight;
            bool mOverflow, mEditing, mAdjustEdit;
            std::uint8_t mPadding;
            int mEditX, mEditY;
            std::uint32_t mEditLabel;
            wchar_t mEditBuffer[162];
            wchar_t mOverflowBuffer[162];
            std::uint32_t mCurrent, mInitial;
            wchar_t mCursor;
            std::uint16_t mCursorPadding;
            int mOverflowStart, mOverflowEnd, mWidth;
            wchar_t mMessageBuffers[14][162];
            wchar_t mFreeBuffers[14];
        };
        static_assert(sizeof(NativeMessageList) == 0x149C);
        static_assert(offsetof(NativeMessageList, mEditBuffer) == 0x28);
        static_assert(offsetof(NativeMessageList, mWidth) == 0x2C4);

        ui::chat::ChatState g_State;
        chat_command::ChatCommandState g_Commands;
        std::atomic<ProcessInput> g_OriginalInput{ nullptr };
        std::atomic<PrintMessage> g_OriginalPrint{ nullptr };
        std::atomic<ConcatMessage> g_OriginalConcat{ nullptr };
        std::atomic<DWORD> g_Thread{ 0 };
        std::atomic<HWND> g_Window{ nullptr };
        std::atomic<bool> g_Available{ false };
        std::atomic<bool> g_Preference{ true };
        std::atomic<bool> g_CleanupRequested{ false };
        bool g_IsInstalled = false;
        bool g_Active = false;
        bool g_NativeSession = false;
        bool g_SavedAdjust = true;
        bool g_NativeFallback = false;
        bool g_ImeCommitted = false;
        bool g_ImeComposing = false;
        std::wstring g_ImeEcho;
        std::size_t g_ImeEchoPosition = 0;
        bool g_SubmitRequested = false;
        bool g_SlashEchoPending = false;
        int g_PrivateEchoRecipient = -1;
        bool g_PrivateEchoObserved = false;
        bool g_FallbackDraftUnverified = false;
        std::wstring g_NativePrefix;
        std::uintptr_t g_Session = 0;
        std::uint64_t g_NowMs = 0;
        DWORD g_SavedRoute = 0;
        UINT g_CleanupMessage = 0;
        wchar_t g_PendingHigh = 0;
        unsigned char g_PendingDbcs = 0;
        std::array<bool, 256> g_ConsumedKeys{};
        POINT g_ImePosition{};
        ui::chat::ImePositionSchedule g_ImePositionSchedule;

        bool IsOwnerThread() noexcept { return g_Thread.load(std::memory_order_acquire) == GetCurrentThreadId(); }
        bool IsEnabled() noexcept { return g_Available.load(std::memory_order_acquire) && g_Preference.load(std::memory_order_acquire); }
        NativeMessageList& NativeList() { return *reinterpret_cast<NativeMessageList*>(profile::MESSAGE_LIST); }
        int GameMode() { return *reinterpret_cast<const int*>(profile::SESSION_MODE); }
        DWORD& NativeRoute() { return *reinterpret_cast<DWORD*>(profile::ROUTE_SELECTOR); }

        void ClearNativePending() noexcept
        {
            *reinterpret_cast<wchar_t*>(profile::PENDING_CHARACTER) = 0;
            *reinterpret_cast<wchar_t*>(profile::IME_TEXT) = 0;
            *reinterpret_cast<int*>(profile::IME_GATE) = 0;
            g_PendingHigh = 0;
            g_PendingDbcs = 0;
        }

        HWND GetGameWindow()
        {
            auto window = g_Window.load(std::memory_order_acquire);
            if (!window) memory::TryReadMemory(0xB73550, &window, sizeof(window));
            if (window) g_Window.store(window, std::memory_order_release);
            return window;
        }

        void CancelComposition(HWND window) noexcept
        {
            g_ImeComposing = false;
            if (!window) return;
            const auto context = ImmGetContext(window);
            if (context)
            {
                ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                ImmReleaseContext(window, context);
            }
        }

        void PositionComposition(HWND window, bool force = false) noexcept
        {
            if (!g_ImePositionSchedule.ShouldAttempt(reinterpret_cast<std::uintptr_t>(window),
                g_ImePosition.x, g_ImePosition.y, GetTickCount64(), force)) return;
            const auto context = ImmGetContext(window);
            if (!context) { g_ImePositionSchedule.CompleteAttempt(false); return; }
            COMPOSITIONFORM composition{};
            composition.dwStyle = CFS_POINT;
            composition.ptCurrentPos = g_ImePosition;
            CANDIDATEFORM candidate{};
            candidate.dwStyle = CFS_CANDIDATEPOS;
            candidate.ptCurrentPos = g_ImePosition;
            const bool compositionSet = ImmSetCompositionWindow(context, &composition) != FALSE;
            const bool candidateSet = ImmSetCandidateWindow(context, &candidate) != FALSE;
            ImmReleaseContext(window, context);
            g_ImePositionSchedule.CompleteAttempt(compositionSet && candidateSet);
        }

        void SetNativeEditHidden(bool hidden)
        {
            const auto label = NativeList().mEditLabel;
            if (label) *reinterpret_cast<bool*>(label + 0x40) = hidden;
        }

        void RestoreBridge() noexcept
        {
            if (!g_NativeSession) return;
            NativeList().mAdjustEdit = g_SavedAdjust;
            NativeRoute() = g_SavedRoute;
            g_NativeSession = false;
            g_NativeFallback = false;
            ClearNativePending();
        }

        void CloseEditor() noexcept
        {
            if (g_NativeSession)
            {
                CancelComposition(GetGameWindow());
                if (NativeList().mEditing) reinterpret_cast<RemoveEdit>(profile::REMOVE_EDIT)(&NativeList());
                RestoreBridge();
            }
            g_ImePositionSchedule.Invalidate();
            g_SubmitRequested = false;
            g_SlashEchoPending = false;
            g_FallbackDraftUnverified = false;
            g_State.End();
            g_ImeCommitted = false;
            g_ImeComposing = false;
            g_ImeEcho.clear();
            g_ImeEchoPosition = 0;
        }

        bool ReadWide(const wchar_t* source, std::size_t limit, std::wstring& output)
        {
            if (!source) return false;
            output.clear();
            for (std::size_t index = 0; index < limit; ++index)
            {
                wchar_t character = 0;
                if (!memory::TryReadMemory(reinterpret_cast<std::uintptr_t>(source + index), &character, sizeof(character))) return false;
                if (!character) return true;
                output += character;
            }
            return false;
        }

        ui::HudColor PlayerColor(int color)
        {
            // 与聊天正文分离的可读色板；不把不完整的 ColorSchemeClass ABI 引入消息快照。
            constexpr std::array<ui::HudColor, 9> COLORS = {{{255,226,80},{245,90,90},{100,155,255},
                {110,220,120},{255,170,80},{100,225,225},{200,135,255},{245,160,210},{220,220,220}}};
            return color >= 0 && color < static_cast<int>(COLORS.size()) ? COLORS[static_cast<std::size_t>(color)] : ui::HudColor{};
        }

        bool CaptureMessage(const wchar_t* sender, const wchar_t* text, int color, bool localEcho = false)
        {
            if (!IsEnabled() || !IsOwnerThread() || !g_Active) return false;
            try
            {
                std::wstring copiedSender, copiedText;
                if (!ReadWide(sender, 64, copiedSender)) return false;
                const int localId = GetLocalChatPlayerId();
                const int playerId = localEcho ? localId : ResolveIncomingChatPlayer(copiedSender, color);
                const bool isLocal = chat_command_profile::IsLocalSender(localEcho, playerId, localId);
                // 屏蔽是有意消费，不能用 false 让原生列表重新漏出同一条聊天。
                if (playerId >= 0 && !isLocal && g_Commands.IsIgnored(playerId)) return true;
                if (!ReadWide(text, 112, copiedText)) return false;
                if (localEcho && g_PrivateEchoRecipient >= 0)
                    copiedText = L"[私聊→#" + std::to_wstring(g_PrivateEchoRecipient + 1) + L"] " + copiedText;
                return g_State.Append(std::move(copiedSender), std::move(copiedText), PlayerColor(color), g_NowMs, playerId, isLocal);
            }
            catch (...) { return false; }
        }

        int __fastcall HookPrint(void* self, void*, const wchar_t* sender, int id, const wchar_t* text,
            int colorScheme, int style, int timeout, char silent)
        {
            const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            const bool localEcho = returnAddress == profile::ECHO_RETURN;
            if (localEcho && IsOwnerThread() && g_PrivateEchoRecipient >= 0) g_PrivateEchoObserved = true;
            if (self == &NativeList() && profile::IsRegularPrint(returnAddress))
            {
                if (CaptureMessage(sender, text, id, localEcho)) return 0;
                // 已确定是本机回显且用户要求隐藏姓名时，分配/读取失败也不能退回原生真实姓名。
                if (localEcho && IsOwnerThread() && IsEnabled() && g_Commands.IsSelfMasked()) return 0;
            }
            const auto original = g_OriginalPrint.load(std::memory_order_acquire);
            return original ? original(self, sender, id, text, colorScheme, style, timeout, silent) : 0;
        }

        int __fastcall HookConcat(void* self, void*, const wchar_t* sender, int id, const wchar_t* text, int timeout)
        {
            const auto returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            // 在 Concat 修改旧标签之前接管；返回成功使原生分支不再调用 Print，避免双份记录。
            if (self == &NativeList() && profile::IsRegularConcat(returnAddress) && CaptureMessage(sender, text, id)) return 1;
            const auto original = g_OriginalConcat.load(std::memory_order_acquire);
            return original ? original(self, sender, id, text, timeout) : 0;
        }

        bool HasBeaconEditor()
        {
            std::uintptr_t player = 0;
            int index = -1;
            if (!memory::TryReadMemory(profile::LOCAL_PLAYER, &player, sizeof(player)) ||
                !memory::TryReadField(player, 0x30, index)) return true;
            return reinterpret_cast<FindBeacon>(profile::FIND_BEACON)(reinterpret_cast<void*>(profile::BEACON_MANAGER), index) != nullptr;
        }

        int LocalColor() { return *reinterpret_cast<const int*>(profile::PLAYER_COLOR); }

        std::wstring LocalName()
        {
            std::uintptr_t player = 0;
            std::wstring name;
            if (memory::TryReadMemory(profile::LOCAL_PLAYER, &player, sizeof(player)) && player)
                ReadWide(reinterpret_cast<const wchar_t*>(player + 0x1602A), 32, name);
            return name.empty() ? L"玩家" : name;
        }

        void BeginEditor(DWORD* input, ProcessInput original, int opener)
        {
            auto& list = NativeList();
            g_SavedAdjust = list.mAdjustEdit;
            g_SavedRoute = NativeRoute();
            list.mAdjustEdit = false;
            g_NativeSession = true;
            if (ui::chat::GetSubmitRoute(GameMode()) == ui::chat::SubmitRoute::LocalOnly)
            {
                if (!profile::OpenLocalEditor(
                    [](std::uintptr_t address, void* output, std::size_t size) { return memory::TryReadMemory(address, output, size); },
                    [&list](int colorScheme, const wchar_t* label)
                    { return reinterpret_cast<AddEdit>(profile::ADD_EDIT)(&list, colorScheme, 0x4046, label, 0, -1) != nullptr; }))
                {
                    RestoreBridge();
                    return;
                }
                *input = 0;
            }
            else if (opener == '/')
            {
                // 原生只接受 Enter/Home/End 开框；斜杠使用 Enter 路由，不把 '/' 交给发送分支。
                chat_command_profile::OpenNetworkSlashEditor(*input, original);
            }
            else original(input);
            if (!list.mEditing) { RestoreBridge(); return; }
            SetNativeEditHidden(true);
            ClearNativePending();
            g_State.Begin(ui::chat::GetSubmitRoute(GameMode()) == ui::chat::SubmitRoute::LocalOnly ? L"本地聊天" :
                opener == 8 ? L"盟友" : opener == 92 ? L"所有玩家" : L"所选接收者");
            g_NativePrefix.assign(list.mEditBuffer, list.mInitial <= 64 ? list.mInitial : 0);
            g_SlashEchoPending = opener == '/';
            if (g_SlashEchoPending) g_State.Insert(L"/");
            g_ImeCommitted = false;
            g_ImeComposing = false;
            PositionComposition(GetGameWindow(), true);
        }

        bool IsNativeImeFinished(HWND window)
        {
            if (*reinterpret_cast<const int*>(profile::IME_GATE)) return false;
            const auto context = window ? ImmGetContext(window) : nullptr;
            if (!context) return true;
            const auto size = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
            ImmReleaseContext(window, context);
            return size == 0;
        }

        void ReclaimFallbackForSubmit(DWORD* input)
        {
            auto& list = NativeList();
            std::wstring draft, pending;
            const bool complete = chat_command_profile::ReadFallbackDraft(
                [](std::uintptr_t address, void* output, std::size_t size) { return memory::TryReadMemory(address, output, size); },
                reinterpret_cast<std::uintptr_t>(list.mEditBuffer), list.mInitial, list.mCurrent,
                list.mOverflow, list.mOverflowBuffer[0], draft) &&
                ReadWide(reinterpret_cast<const wchar_t*>(profile::PENDING_CHARACTER), 256, pending);
            const bool imeFinished = IsNativeImeFinished(GetGameWindow());
            g_State.Begin(ui::chat::GetSubmitRoute(GameMode()) == ui::chat::SubmitRoute::LocalOnly ? L"本地聊天" :
                NativeRoute() == 2 ? L"盟友" : NativeRoute() == 3 ? L"所有玩家" : L"所选接收者");
            g_ImeCommitted = false;
            g_ImeComposing = false;
            g_ImeEcho.clear();
            g_ImeEchoPosition = 0;
            g_SlashEchoPending = false;
            if (complete) { g_State.Insert(draft); g_State.Insert(pending); }
            g_FallbackDraftUnverified = !complete;
            g_NativeFallback = false;
            SetNativeEditHidden(true);
            CancelComposition(GetGameWindow());
            ClearNativePending();
            if (input) *input = 0;
            if (!complete) g_State.SetStatus(L"原生草稿不完整或已溢出，未发送。请按 Esc 关闭后重新输入完整内容。");
            else if (!imeFinished) g_State.SetStatus(L"输入法尚未确认结束，请核对已提交文字后再按一次 Enter。");
            else g_SubmitRequested = true;
        }

        void __fastcall HookInput(DWORD* input, void*)
        {
            const auto original = g_OriginalInput.load(std::memory_order_acquire);
            if (!original) return;
            if (!IsOwnerThread()) { original(input); return; }
            if (g_CleanupRequested.exchange(false, std::memory_order_acq_rel)) CloseEditor();
            if (g_NativeFallback && !NativeList().mEditing) RestoreBridge();
            if (!IsEnabled() || !g_Active) { if (g_NativeSession) CloseEditor(); original(input); return; }
            if (g_NativeFallback && NativeList().mEditing &&
                (NativeList().mOverflow || NativeList().mOverflowBuffer[0] ||
                 (input && (*input & 0x800) == 0 && (*input & 0xFF) == VK_RETURN)))
            {
                try { ReclaimFallbackForSubmit(input); }
                catch (...) { if (input) *input = 0; CloseEditor(); }
                return;
            }
            if (g_State.IsEditing()) { if (input) *input = 0; ClearNativePending(); return; }
            if (!input || !*input || NativeList().mEditing || (*input & 0x8800)) { original(input); return; }
            void* keyboard = nullptr;
            if (!memory::TryReadMemory(profile::KEYBOARD_INSTANCE, &keyboard, sizeof(keyboard)) || !keyboard) { original(input); return; }
            const auto opener = static_cast<unsigned char>(reinterpret_cast<TranslateKey>(profile::TRANSLATE_KEY)(keyboard, static_cast<std::uint16_t>(*input)));
            if ((opener != 13 && opener != 8 && opener != 92 && opener != '/') || HasBeaconEditor()) { original(input); return; }
            const auto savedInput = *input;
            try { BeginEditor(input, original, opener); }
            catch (...)
            {
                CloseEditor();
                *input = savedInput;
                original(input);
            }
        }

        bool BuildNativeDraft(std::wstring& output, std::wstring_view draft)
        {
            const auto& list = NativeList();
            if (g_FallbackDraftUnverified || !g_NativeSession || !list.mEditing || list.mInitial > 64 || draft.size() > ui::chat::MAX_DRAFT_UNITS ||
                list.mInitial + draft.size() >= std::size(list.mEditBuffer) || list.mMaxCharacters < 2 ||
                draft.size() >= static_cast<std::size_t>(list.mMaxCharacters)) return false;
            output.assign(list.mEditBuffer, list.mInitial);
            output += draft;
            void* font = nullptr;
            return memory::TryReadMemory(profile::FONT_INSTANCE, &font, sizeof(font)) && font && list.mWidth > 10 &&
                reinterpret_cast<TextWidth>(profile::TEXT_WIDTH)(font, output.c_str(), 0) < list.mWidth - 10;
        }

        bool SynchronizeNativeDraft(std::wstring_view draft)
        {
            std::wstring combined;
            if (!BuildNativeDraft(combined, draft)) return false;
            auto& list = NativeList();
            std::copy(combined.begin(), combined.end(), list.mEditBuffer);
            list.mEditBuffer[combined.size()] = 0;
            list.mCurrent = static_cast<std::uint32_t>(combined.size());
            return true;
        }

        void FinishCommand(std::wstring feedback = {})
        {
            CloseEditor();
            if (!feedback.empty()) g_State.Append(L"插件", std::move(feedback), {}, g_NowMs);
        }

        void Submit()
        {
            g_SubmitRequested = false;
            if (!g_State.IsEditing()) return;
            if (g_FallbackDraftUnverified)
            { g_State.SetStatus(L"原生草稿未能完整恢复，未发送。请按 Esc 关闭后重新输入。"); return; }
            if (g_State.GetDraft().empty()) { CloseEditor(); return; }
            const std::wstring submittedDraft = g_State.GetDraft();
            const bool isCommand = chat_command::IsChatCommandDraft(submittedDraft);
            // 失败命令也可召回重试；私聊记录完整命令，不用正文替代。
            if (isCommand) g_State.RecordSubmittedDraft(submittedDraft);
            const auto command = g_Commands.Execute(submittedDraft, CaptureChatCommandPlayers(), NextChatCommandRandomChoice());
            if (!command.mSucceeded) { FinishCommand(command.mFeedback); return; }
            if (command.mAction == chat_command::CommandAction::Feedback)
            {
                FinishCommand(command.mFeedback);
                return;
            }
            if (command.mAction == chat_command::CommandAction::ChangeSpeed)
            {
                std::wstring feedback;
                TryChangeGameChatSpeed(command.mSpeed, feedback);
                FinishCommand(std::move(feedback));
                return;
            }
            if (command.mAction == chat_command::CommandAction::SendPrivate)
            {
                const int localId = GetLocalChatPlayerId();
                if (!command.mRecipient) { FinishCommand(L"私聊目标无效，未发送。"); return; }
                const int target = *command.mRecipient;
                if (target == localId && localId >= 0)
                {
                    const bool appended = g_State.Append(LocalName(), L"[私聊→#" + std::to_wstring(target + 1) + L"] " + command.mText,
                        PlayerColor(LocalColor()), g_NowMs, localId, true);
                    FinishCommand(appended ? L"" : L"私聊内容无法加入本地历史，未发送。");
                    return;
                }
                if (chat_command::IsChatCommandDraft(command.mText))
                {
                    FinishCommand(L"当前原生私聊不支持以斜杠命令开头的正文，请改写后发送。");
                    return;
                }
                std::wstring error;
                const bool submitted = WithPrivateChatRecipient(target, [&]
                {
                    if (!SynchronizeNativeDraft(command.mText)) return false;
                    struct EchoScope
                    {
                        int mPrevious;
                        ~EchoScope() { g_PrivateEchoRecipient = mPrevious; }
                    } scope{ g_PrivateEchoRecipient };
                    g_PrivateEchoRecipient = target;
                    g_PrivateEchoObserved = false;
                    DWORD enter = VK_RETURN;
                    const auto original = g_OriginalInput.load(std::memory_order_acquire);
                    if (!original) return false;
                    original(&enter);
                    return g_PrivateEchoObserved;
                }, error);
                FinishCommand(submitted ? L"" : error.empty() ? L"私聊未能提交，请重新输入后重试。" : std::move(error));
                return;
            }
            if (!SynchronizeNativeDraft(command.mText))
            {
                if (isCommand) FinishCommand(L"内容超过当前聊天框的长度或宽度，未发送。");
                else g_State.SetStatus(L"内容超过当前聊天框的长度或宽度，请缩短后发送。");
                return;
            }
            if (ui::chat::GetSubmitRoute(GameMode()) == ui::chat::SubmitRoute::LocalOnly)
            {
                if (g_State.Append(LocalName(), command.mText, PlayerColor(LocalColor()), g_NowMs, GetLocalChatPlayerId(), true))
                {
                    if (!isCommand) g_State.RecordSubmittedDraft(submittedDraft);
                    CloseEditor();
                }
                else if (isCommand) FinishCommand(L"内容无法加入本地历史，未发送。");
                return;
            }
            // 通过原生完整发送流程保留频道、观察者和接收者过滤；不自行构造广播包。
            DWORD enter = VK_RETURN;
            const auto original = g_OriginalInput.load(std::memory_order_acquire);
            if (original)
            {
                original(&enter);
                // 原生 void 入口结束编辑只代表本机已提交，不代表远端确认送达。
                if (!isCommand && !NativeList().mEditing) g_State.RecordSubmittedDraft(submittedDraft);
            }
            CloseEditor();
        }

        bool SetClipboard(HWND window, const std::wstring& text)
        {
            if (text.empty() || !OpenClipboard(window)) return false;
            const auto memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
            bool copied = false;
            if (memory)
            {
                const auto destination = GlobalLock(memory);
                if (destination)
                {
                    std::memcpy(destination, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
                    GlobalUnlock(memory);
                    if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) copied = true;
                }
                if (!copied) GlobalFree(memory);
            }
            CloseClipboard();
            return copied;
        }

        void Paste(HWND window)
        {
            if (!OpenClipboard(window)) return;
            const auto memory = GetClipboardData(CF_UNICODETEXT);
            if (memory)
            {
                const auto source = static_cast<const wchar_t*>(GlobalLock(memory));
                const auto size = (std::min)(static_cast<std::size_t>(GlobalSize(memory) / sizeof(wchar_t)), std::size_t{32768});
                if (source)
                {
                    const auto length = wcsnlen_s(source, size);
                    try { g_State.Insert(std::wstring_view(source, length)); }
                    catch (...) { GlobalUnlock(memory); CloseClipboard(); throw; }
                    GlobalUnlock(memory);
                }
            }
            CloseClipboard();
        }

        bool ReadComposition(HWND window, DWORD kind, std::wstring& output)
        {
            const auto context = ImmGetContext(window);
            if (!context) return false;
            const auto size = ImmGetCompositionStringW(context, kind, nullptr, 0);
            if (size < 0 || size > 4096 || (size % sizeof(wchar_t))) { ImmReleaseContext(window, context); return false; }
            try
            {
                output.resize(static_cast<std::size_t>(size) / sizeof(wchar_t));
                const auto read = size ? ImmGetCompositionStringW(context, kind, output.data(), static_cast<DWORD>(size)) : 0;
                ImmReleaseContext(window, context);
                return read == size;
            }
            catch (...) { ImmReleaseContext(window, context); throw; }
        }

        bool FallBackToNativeInput() noexcept
        {
            // 未覆盖的 IME 保留原生输入；不继续隐藏用户已输入的正文。
            try
            {
                // 斜杠命令只能由本机命令层处理；IME 回退不能把未知命令或私聊前缀送到原生广播。
                if (chat_command::IsChatCommandDraft(g_State.GetDraft()))
                {
                    CancelComposition(GetGameWindow());
                    g_State.SetComposition(L"");
                    g_State.SetStatus(L"聊天命令不回退到原生发送，请切换输入法后继续编辑。");
                    return false;
                }
                if (!SynchronizeNativeDraft(g_State.GetDraft()))
                {
                    CancelComposition(GetGameWindow());
                    g_State.SetComposition(L"");
                    g_State.SetStatus(L"当前输入法无法接管这段较长文字，请缩短内容或切换输入法后继续。");
                    return false;
                }
                SetNativeEditHidden(false);
                g_State.End();
                g_NativeFallback = true;
                return true;
            }
            catch (...) { CloseEditor(); return false; }
        }

        void InsertCharacter(wchar_t character)
        {
            if (chat_command_profile::ConsumeOpeningSlashEcho(g_SlashEchoPending, character)) return;
            if (g_ImeEchoPosition < g_ImeEcho.size())
            {
                if (g_ImeEcho[g_ImeEchoPosition] == character) { ++g_ImeEchoPosition; return; }
                g_ImeEcho.clear();
                g_ImeEchoPosition = 0;
            }
            if (character >= 0xD800 && character <= 0xDBFF) { g_PendingHigh = character; return; }
            if (character >= 0xDC00 && character <= 0xDFFF && g_PendingHigh)
            {
                const wchar_t pair[] = { g_PendingHigh, character };
                g_State.Insert(std::wstring_view(pair, 2));
            }
            else if (character >= 32 && character != 127) g_State.Insert(std::wstring_view(&character, 1));
            g_PendingHigh = 0;
        }

        void InsertAnsiCharacter(WPARAM value)
        {
            char bytes[2]{};
            int count = 1;
            if (HIBYTE(value)) { bytes[0] = static_cast<char>(HIBYTE(value)); bytes[1] = static_cast<char>(LOBYTE(value)); count = 2; }
            else if (g_PendingDbcs) { bytes[0] = static_cast<char>(g_PendingDbcs); bytes[1] = static_cast<char>(value); g_PendingDbcs = 0; count = 2; }
            else
            {
                if (IsDBCSLeadByteEx(CP_ACP, static_cast<BYTE>(value))) { g_PendingDbcs = static_cast<unsigned char>(value); return; }
                bytes[0] = static_cast<char>(value);
            }
            wchar_t characters[2]{};
            const auto converted = MultiByteToWideChar(CP_ACP, 0, bytes, count, characters, 2);
            for (int index = 0; index < converted; ++index) InsertCharacter(characters[index]);
        }

        std::string HexAddress(std::uintptr_t address)
        {
            char buffer[16]{};
            std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(address));
            return buffer;
        }

        std::string HexBytes(const std::array<std::uint8_t, 12>& bytes, std::size_t size)
        {
            std::string result;
            char hex[4]{};
            for (std::size_t index = 0; index < size; ++index)
            {
                std::snprintf(hex, sizeof(hex), "%02X", static_cast<unsigned int>(bytes[index]));
                if (index) result += ' ';
                result += hex;
            }
            return result;
        }

        std::string ModuleOwner(std::uintptr_t address)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(address), &module)) return "unowned/private";
            wchar_t path[32768]{};
            const auto length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
            if (!length || length >= std::size(path)) return "module@" + HexAddress(reinterpret_cast<std::uintptr_t>(module));
            const auto bytes = WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
            std::string owner(static_cast<std::size_t>(bytes), '\0');
            if (bytes) WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(length), owner.data(), bytes, nullptr, nullptr);
            return owner + "@" + HexAddress(reinterpret_cast<std::uintptr_t>(module));
        }

        void ReportProfileMismatch(const profile::EntryProbe& probe, const std::array<std::uint8_t, 12>& actual,
            bool readable, std::string& outError)
        {
            outError += "; ";
            outError += probe.mName;
            outError += "@" + HexAddress(probe.mAddress) + " expected=[" + HexBytes(probe.mExpected, probe.mSize) + "] actual=[";
            if (!readable) { outError += "unreadable]"; return; }
            outError += HexBytes(actual, probe.mSize) + "]";
            std::uintptr_t target = 0;
            if (actual[0] == 0xE8 || actual[0] == 0xE9)
            {
                std::int32_t displacement = 0;
                std::memcpy(&displacement, actual.data() + 1, sizeof(displacement));
                target = probe.mAddress + 5 + displacement;
            }
            else if (actual[0] == 0xEB) target = probe.mAddress + 2 + static_cast<std::int8_t>(actual[1]);
            else if (actual[0] == 0xFF && actual[1] == 0x25)
            {
                std::uint32_t pointerAddress = 0;
                std::memcpy(&pointerAddress, actual.data() + 2, sizeof(pointerAddress));
                memory::TryReadMemory(pointerAddress, &target, sizeof(target));
            }
            if (target)
            {
                outError += " branch=" + HexAddress(target) + " owner=" + ModuleOwner(target);
                std::array<std::uint8_t, 12> head{};
                if (memory::TryReadMemory(target, head.data(), head.size())) outError += " target-head=[" + HexBytes(head, head.size()) + "]";
            }
        }
    }

    bool InstallGameChatUi(std::string& outError)
    {
        if (g_IsInstalled)
        {
            g_Thread.store(GetCurrentThreadId(), std::memory_order_release);
            g_Available.store(true, std::memory_order_release);
            outError.clear();
            return true;
        }
        if (!IsSupportedHost(outError)) return false;
        outError = "native chat profile differs from supported target";
        if (!profile::ValidateProfile(
            [](std::uintptr_t address, void* output, std::size_t size) { return memory::TryReadMemory(address, output, size); },
            [&outError](const auto& probe, const auto& actual, bool readable) { ReportProfileMismatch(probe, actual, readable, outError); })) return false;
        const auto initialized = MH_Initialize();
        if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) { outError = "chat MinHook initialization failed"; return false; }
        const std::array<std::uintptr_t, 3> addresses = { profile::PROCESS_INPUT, profile::PRINT_MESSAGE, profile::CONCAT_MESSAGE };
        const std::array<void*, 3> detours = { reinterpret_cast<void*>(&HookInput), reinterpret_cast<void*>(&HookPrint), reinterpret_cast<void*>(&HookConcat) };
        std::array<void*, 3> originals{};
        std::size_t created = 0;
        for (; created < addresses.size(); ++created)
            if (MH_CreateHook(reinterpret_cast<void*>(addresses[created]), detours[created], &originals[created]) != MH_OK) break;
        if (created != addresses.size())
        {
            for (std::size_t index = 0; index < created; ++index) MH_RemoveHook(reinterpret_cast<void*>(addresses[index]));
            outError = "cannot create native chat hooks";
            return false;
        }
        g_OriginalInput.store(reinterpret_cast<ProcessInput>(originals[0]), std::memory_order_release);
        g_OriginalPrint.store(reinterpret_cast<PrintMessage>(originals[1]), std::memory_order_release);
        g_OriginalConcat.store(reinterpret_cast<ConcatMessage>(originals[2]), std::memory_order_release);
        g_CleanupMessage = RegisterWindowMessageW(L"RACommandsPlugin.ChatUi.Cleanup.7CD005D2");
        if (!g_CleanupMessage)
        {
            for (const auto address : addresses) MH_RemoveHook(reinterpret_cast<void*>(address));
            outError = "cannot register chat cleanup message";
            return false;
        }
        g_Thread.store(GetCurrentThreadId(), std::memory_order_release);
        for (const auto address : addresses)
        {
            if (MH_EnableHook(reinterpret_cast<void*>(address)) != MH_OK)
            {
                for (const auto target : addresses) MH_DisableHook(reinterpret_cast<void*>(target));
                outError = "cannot enable native chat hooks";
                return false;
            }
        }
        g_IsInstalled = true;
        g_Available.store(true, std::memory_order_release);
        outError.clear();
        return true;
    }

    void UpdateGameChatUi(std::uint64_t nowMs, bool isSessionActive, std::uintptr_t sessionIdentity)
    {
        if (!IsOwnerThread()) return;
        g_NowMs = nowMs;
        if (g_CleanupRequested.exchange(false, std::memory_order_acq_rel) || !IsEnabled()) CloseEditor();
        if (!isSessionActive || !sessionIdentity || sessionIdentity != g_Session)
        {
            CloseEditor();
            g_State.Reset();
            g_Commands.Reset();
            g_Session = isSessionActive ? sessionIdentity : 0;
        }
        g_Active = isSessionActive && sessionIdentity;
        if (g_NativeFallback && !NativeList().mEditing) RestoreBridge();
        if (g_SubmitRequested && g_Active && IsEnabled()) Submit();
    }

    ui::ChatView CaptureGameChatUi(std::uint64_t)
    {
        return IsOwnerThread() && g_Active && IsEnabled() ? g_State.Capture(IsEnabled(), g_Commands.IsSelfMasked(), g_Commands.GetSelfMaskName()) : ui::ChatView{};
    }

    bool HandleGameChatUiMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (!IsOwnerThread()) return false;
        g_Window.store(window, std::memory_order_release);
        // 输入法或焦点切换可替换同一窗口的 IMM context，下一次布局必须重新定位。
        if (message == WM_INPUTLANGCHANGE || message == WM_IME_SETCONTEXT || message == WM_SETFOCUS ||
            message == WM_KILLFOCUS || message == WM_ACTIVATEAPP) g_ImePositionSchedule.Invalidate();
        if (g_CleanupMessage && message == g_CleanupMessage) { g_CleanupRequested.store(false); CloseEditor(); return true; }
        if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wParam))
        {
            g_ConsumedKeys.fill(false);
            if (g_NativeSession) CloseEditor();
            return false;
        }
        if ((message == WM_KEYUP || message == WM_SYSKEYUP) && wParam < g_ConsumedKeys.size() && g_ConsumedKeys[wParam])
        {
            g_ConsumedKeys[wParam] = false;
            return true;
        }
        if (!IsEnabled() || !g_Active) return false;
        if (!g_State.IsEditing())
        {
            // WM_CHAR 可能先于 WWKey 队列被处理。此处立即开框，后续字符不会在等待主帧时丢失。
            if ((message == WM_CHAR || message == WM_UNICHAR) && wParam == '/' && !NativeList().mEditing && !HasBeaconEditor())
            {
                const auto original = g_OriginalInput.load(std::memory_order_acquire);
                if (!original) return false;
                DWORD enter = VK_RETURN;
                try
                {
                    BeginEditor(&enter, original, '/');
                    if (g_State.IsEditing()) { g_SlashEchoPending = false; return true; }
                }
                catch (...) { CloseEditor(); }
            }
            return false;
        }
        try
        {
            if (message == WM_IME_STARTCOMPOSITION)
            {
                g_ImeCommitted = false;
                g_ImeComposing = true;
                g_ImeEcho.clear();
                g_ImeEchoPosition = 0;
                g_State.SetComposition(L"");
                PositionComposition(window, true);
                return false;
            }
            if (message == WM_IME_ENDCOMPOSITION) { g_ImeComposing = false; g_State.SetComposition(L""); return false; }
            if (message == WM_IME_COMPOSITION)
            {
                std::wstring text;
                if (lParam & GCS_RESULTSTR)
                {
                    if (!ReadComposition(window, GCS_RESULTSTR, text)) return !FallBackToNativeInput();
                    g_ImeEcho = text;
                    g_ImeEchoPosition = 0;
                    g_State.Insert(text);
                    g_ImeCommitted = true;
                    g_State.SetComposition(L"");
                }
                if (lParam & GCS_COMPSTR)
                {
                    if (!ReadComposition(window, GCS_COMPSTR, text)) return !FallBackToNativeInput();
                    g_State.SetComposition(std::move(text));
                }
                return true;
            }
            if (message == WM_IME_CHAR)
            {
                if (!g_ImeCommitted)
                {
                    if (IsWindowUnicode(window)) InsertCharacter(static_cast<wchar_t>(wParam));
                    else InsertAnsiCharacter(wParam);
                }
                return true;
            }
            if (message == WM_CHAR)
            {
                if (IsWindowUnicode(window)) InsertCharacter(static_cast<wchar_t>(wParam));
                else InsertAnsiCharacter(wParam);
                return true;
            }
            if (message == WM_UNICHAR)
            {
                if (wParam == UNICODE_NOCHAR) return true;
                if (wParam <= 0xFFFF) InsertCharacter(static_cast<wchar_t>(wParam));
                else if (wParam <= 0x10FFFF)
                {
                    const auto scalar = static_cast<unsigned int>(wParam - 0x10000);
                    InsertCharacter(static_cast<wchar_t>(0xD800 + (scalar >> 10)));
                    InsertCharacter(static_cast<wchar_t>(0xDC00 + (scalar & 0x3FF)));
                }
                return true;
            }
            if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
            {
                if (wParam == VK_PROCESSKEY) return false;
                const bool alt = (lParam & (LPARAM{1} << 29)) != 0 || (GetKeyState(VK_MENU) & 0x8000) != 0;
                const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (ui::chat::ShouldPassSystemShortcut(static_cast<std::uint32_t>(wParam), alt, control)) return false;
                // 候选刚打开时组合串可能仍为空；方向键交给 IME，保留提交回声去重。
                if ((wParam == VK_UP || wParam == VK_DOWN || wParam == VK_RETURN) &&
                    (g_ImeComposing || g_State.HasComposition())) return false;
                if (wParam != VK_UP && wParam != VK_DOWN)
                {
                    g_ImeEcho.clear();
                    g_ImeEchoPosition = 0;
                }
                if (wParam < g_ConsumedKeys.size()) g_ConsumedKeys[wParam] = true;
                const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (control && wParam == 'A') g_State.SelectAll();
                else if (control && wParam == 'C') SetClipboard(window, g_State.CopySelection());
                else if (control && wParam == 'V') { CancelComposition(window); g_State.SetComposition(L""); Paste(window); }
                else if (wParam == VK_ESCAPE) CloseEditor();
                else if (wParam == VK_RETURN)
                {
                    if (!g_State.HasComposition()) g_SubmitRequested = true;
                    else return false;
                }
                else if (wParam == VK_UP) g_State.RecallPreviousDraft();
                else if (wParam == VK_DOWN) g_State.RecallNextDraft();
                else if (wParam == VK_BACK) g_State.Backspace();
                else if (wParam == VK_DELETE) g_State.Delete();
                else if (wParam == VK_LEFT) g_State.Move(-1, shift);
                else if (wParam == VK_RIGHT) g_State.Move(1, shift);
                else if (wParam == VK_HOME) g_State.MoveToEnd(false, shift);
                else if (wParam == VK_END) g_State.MoveToEnd(true, shift);
                return true;
            }
            // 鼠标命中和成对按下/释放由共用 HUD 入口处理；此处不抢占其复制图标及滚动逻辑。
        }
        catch (...) { return !FallBackToNativeInput(); }
        return false;
    }

    bool IsGameChatUiEditing() noexcept { return IsOwnerThread() && g_State.IsEditing() && IsEnabled(); }
    bool IsGameChatUiEnabled() noexcept { return IsEnabled(); }

    void SetGameChatUiEnabled(bool isEnabled)
    {
        if (isEnabled && !IsGameHudUiAvailable()) return;
        g_Preference.store(isEnabled, std::memory_order_release);
        if (!isEnabled)
        {
            g_CleanupRequested.store(true, std::memory_order_release);
            if (IsOwnerThread()) { g_CleanupRequested.store(false); CloseEditor(); }
            else if (const auto window = g_Window.load(std::memory_order_acquire)) PostMessageW(window, g_CleanupMessage, 0, 0);
        }
    }

    bool CopyGameChatUiMessage(HWND window, std::uint64_t messageId)
    {
        return IsOwnerThread() && IsEnabled() && g_State.IsEditing() && SetClipboard(window, g_State.CopyMessage(messageId, g_Commands.IsSelfMasked(), g_Commands.GetSelfMaskName()));
    }

    void SetGameChatUiCaret(std::size_t caret, bool extendSelection)
    {
        if (IsOwnerThread() && IsEnabled()) g_State.SetCaret(caret, extendSelection);
    }

    void SetGameChatUiInputPosition(int clientX, int clientY) noexcept
    {
        if (!IsOwnerThread()) return;
        g_ImePosition = { clientX, clientY };
        if (g_State.IsEditing()) PositionComposition(g_Window.load(std::memory_order_acquire));
    }

    void DisableGameChatUi() noexcept
    {
        g_Available.store(false, std::memory_order_release);
        g_CleanupRequested.store(true, std::memory_order_release);
        if (IsOwnerThread()) { g_CleanupRequested.store(false); CloseEditor(); }
        else if (const auto window = g_Window.load(std::memory_order_acquire)) PostMessageW(window, g_CleanupMessage, 0, 0);
    }
}
