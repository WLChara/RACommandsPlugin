#include "Commands/AutoSuperWeapon/AutoSuperWeaponCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class AutoSuperWeaponToggleRegistry final : public NativeCommandRegistry
        {
        public:
            AutoSuperWeaponToggleRegistry(NativeCommandMetadata metadata,
                AutoSuperWeaponExecuteCallback callback)
                : NativeCommandRegistry(metadata), mCallback(callback)
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
            AutoSuperWeaponExecuteCallback mCallback;
            mutable bool mHeld = false;
            mutable DWORD mHeldKey = 0;
        };

        AutoSuperWeaponToggleRegistry* g_IronCurtainRegistry = nullptr;
        AutoSuperWeaponToggleRegistry* g_RageInductorRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterAutoIronCurtainCommand(
        const GameSymbols& symbols, AutoSuperWeaponExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "auto iron-curtain callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_IronCurtainRegistry)
        {
            g_IronCurtainRegistry = new AutoSuperWeaponToggleRegistry({
                "RAAutoIronCurtain",
                L"自动释放铁幕",
                L"插件",
                L"开启或关闭对选中地面载具的自动铁幕释放"
            }, callback);
        }
        return g_IronCurtainRegistry->TryRegister(symbols, outError);
    }

    CommandRegistrationResult TryRegisterAutoRageInductorCommand(
        const GameSymbols& symbols, AutoSuperWeaponExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "auto rage-inductor callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_RageInductorRegistry)
        {
            g_RageInductorRegistry = new AutoSuperWeaponToggleRegistry({
                "RAAutoRageInductor",
                L"自动释放狂暴",
                L"插件",
                L"开启或关闭对选中步兵和地面载具的自动狂暴释放"
            }, callback);
        }
        return g_RageInductorRegistry->TryRegister(symbols, outError);
    }

    void DisableAutoIronCurtainCommand()
    {
        if (g_IronCurtainRegistry)
        {
            g_IronCurtainRegistry->ResetInput();
            g_IronCurtainRegistry->Disable();
        }
    }

    void DisableAutoRageInductorCommand()
    {
        if (g_RageInductorRegistry)
        {
            g_RageInductorRegistry->ResetInput();
            g_RageInductorRegistry->Disable();
        }
    }
}
