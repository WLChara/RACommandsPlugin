#include "Commands/ForceShieldCommand/ForceShieldCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class ForceShieldCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit ForceShieldCommandRegistry(ForceShieldExecuteCallback callback)
                : NativeCommandRegistry({
                    "RAForceShieldAtCursor",
                    L"在鼠标处释放力场护盾",
                    L"插件",
                    L"将已就绪的本方力场护盾释放到鼠标指向的地图格"
                }), mCallback(callback)
            {
            }

            bool vt_entry_18(DWORD) const override
            {
                return true;
            }

            void ResetInput() const noexcept
            {
                mHeld = false;
            }

        protected:
            void OnExecute(DWORD argument) const override
            {
                constexpr DWORD RELEASE_FLAG = 0x800u;
                constexpr DWORD KEY_CODE_MASK = 0xFFu;
                const DWORD keyCode = argument & KEY_CODE_MASK;
                if (argument & RELEASE_FLAG)
                {
                    if (mHeld && keyCode == mHeldKey)
                    {
                        mHeld = false;
                    }
                    return;
                }
                if (mHeld && keyCode == mHeldKey)
                {
                    return;
                }
                mHeld = true;
                mHeldKey = keyCode;
                if (mCallback)
                {
                    mCallback();
                }
            }

        private:
            ForceShieldExecuteCallback mCallback;
            mutable bool mHeld = false;
            mutable DWORD mHeldKey = 0;
        };

        ForceShieldCommandRegistry* g_ForceShieldCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterForceShieldCommand(
        const GameSymbols& symbols, ForceShieldExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "force-shield callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_ForceShieldCommandRegistry)
        {
            g_ForceShieldCommandRegistry = new ForceShieldCommandRegistry(callback);
        }
        return g_ForceShieldCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableForceShieldCommand()
    {
        if (g_ForceShieldCommandRegistry)
        {
            g_ForceShieldCommandRegistry->ResetInput();
            g_ForceShieldCommandRegistry->Disable();
        }
    }
}
