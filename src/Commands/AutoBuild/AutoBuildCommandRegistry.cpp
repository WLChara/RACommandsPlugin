#include "Commands/AutoBuild/AutoBuildCommandRegistry.h"

#include "Commands/AutoBuild/AutoBuildKeyPressTracker.h"
#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class AutoBuildToggleRegistry final : public NativeCommandRegistry
        {
        public:
            AutoBuildToggleRegistry(NativeCommandMetadata metadata,
                AutoBuildExecuteCallback callback)
                : NativeCommandRegistry(metadata), mCallback(callback)
            {
            }

            // 接收 key-up，才可区分第二次按键与长按自动重复。
            bool vt_entry_18(DWORD) const override
            {
                return true;
            }

            void ResetInput() const noexcept
            {
                mInput.Reset();
            }

        protected:
            void OnExecute(DWORD argument) const override
            {
                if (mCallback && mInput.Consume(argument))
                {
                    mCallback();
                }
            }

        private:
            AutoBuildExecuteCallback mCallback;
            mutable auto_build::AutoBuildKeyPressTracker mInput;
        };

        // 原生命令表与热键表在进程存续期间持有这两个对象。
        AutoBuildToggleRegistry* g_MainAutoBuildRegistry = nullptr;
        AutoBuildToggleRegistry* g_DefenseAutoBuildRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterMainAutoBuildCommand(
        const GameSymbols& symbols, AutoBuildExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "main auto-build callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_MainAutoBuildRegistry)
        {
            g_MainAutoBuildRegistry = new AutoBuildToggleRegistry({
                "RAAutoContinueMainBuild",
                L"主建造栏自动续建",
                L"插件",
                L"开启或关闭主建造栏自动续建；记录当前或下次手动建造的建筑"
            }, callback);
        }
        return g_MainAutoBuildRegistry->TryRegister(symbols, outError);
    }

    CommandRegistrationResult TryRegisterDefenseAutoBuildCommand(
        const GameSymbols& symbols, AutoBuildExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "defense auto-build callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_DefenseAutoBuildRegistry)
        {
            g_DefenseAutoBuildRegistry = new AutoBuildToggleRegistry({
                "RAAutoContinueDefenseBuild",
                L"防御建造栏自动续建",
                L"插件",
                L"开启或关闭防御建造栏自动续建；记录当前或下次手动建造的建筑"
            }, callback);
        }
        return g_DefenseAutoBuildRegistry->TryRegister(symbols, outError);
    }

    void DisableMainAutoBuildCommand()
    {
        if (g_MainAutoBuildRegistry)
        {
            g_MainAutoBuildRegistry->ResetInput();
            g_MainAutoBuildRegistry->Disable();
        }
    }

    void DisableDefenseAutoBuildCommand()
    {
        if (g_DefenseAutoBuildRegistry)
        {
            g_DefenseAutoBuildRegistry->ResetInput();
            g_DefenseAutoBuildRegistry->Disable();
        }
    }
}
