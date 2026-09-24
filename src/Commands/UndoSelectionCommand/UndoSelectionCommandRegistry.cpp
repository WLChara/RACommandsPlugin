#include "Commands/UndoSelectionCommand/UndoSelectionCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class UndoSelectionCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit UndoSelectionCommandRegistry(void(*callback)())
                : NativeCommandRegistry({
                    "RAUndoSelection",
                    L"撤销选择",
                    L"插件",
                    L"每次按键恢复一条历史选择记录，最多保留最近 12 次"
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
        UndoSelectionCommandRegistry* g_UndoSelectionCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterUndoSelectionCommand(
        const GameSymbols& symbols,
        void(*callback)(),
        std::string& outError)
    {
        if (!callback)
        {
            outError = "undo selection callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_UndoSelectionCommandRegistry)
        {
            g_UndoSelectionCommandRegistry = new UndoSelectionCommandRegistry(callback);
        }
        return g_UndoSelectionCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableUndoSelectionCommand()
    {
        if (g_UndoSelectionCommandRegistry)
        {
            g_UndoSelectionCommandRegistry->Disable();
        }
    }
}
