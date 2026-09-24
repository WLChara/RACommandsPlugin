#include "Commands/PassengerSelectCommand/PassengerSelectCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class PassengerSelectCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit PassengerSelectCommandRegistry(void(*callback)())
                : NativeCommandRegistry({
                    "RAPassengerSelect",
                    L"按载员筛选",
                    L"插件",
                    L"每次按键轮换筛选满载、未满载与空载单位"
                }),
                mCallback(callback)
            {
            }

        protected:
            void OnExecute(DWORD) const override
            {
                if (mCallback)
                {
                    mCallback();
                }
            }

        private:
            void(*mCallback)();
        };

        // 游戏命令表与热键表会持有 this，因此实例保持到进程退出。
        PassengerSelectCommandRegistry* g_PassengerSelectCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterPassengerSelectCommand(
        const GameSymbols& symbols,
        void(*callback)(),
        std::string& outError)
    {
        if (!callback)
        {
            outError = "passenger selection callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_PassengerSelectCommandRegistry)
        {
            g_PassengerSelectCommandRegistry = new PassengerSelectCommandRegistry(callback);
        }
        return g_PassengerSelectCommandRegistry->TryRegister(symbols, outError);
    }

    void DisablePassengerSelectCommand()
    {
        if (g_PassengerSelectCommandRegistry)
        {
            g_PassengerSelectCommandRegistry->Disable();
        }
    }
}
