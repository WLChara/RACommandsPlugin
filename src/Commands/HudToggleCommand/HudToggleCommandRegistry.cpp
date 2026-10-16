#include "Commands/HudToggleCommand/HudToggleCommandRegistry.h"
#include "Commands/CommandCategories.h"
#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class HudToggleCommandRegistry final : public NativeCommandRegistry
        {
        public:
            HudToggleCommandRegistry(NativeCommandMetadata metadata, HudToggleCallback callback)
                : NativeCommandRegistry(metadata), mCallback(callback) {}

        protected:
            void OnExecute(DWORD) const override
            {
                if (mCallback) mCallback();
            }

        private:
            HudToggleCallback mCallback;
        };

        // 原生命令表长期引用注册对象；关闭插件只停用，不释放这些对象。
        HudToggleCommandRegistry* g_BeaconCommand = nullptr;
        HudToggleCommandRegistry* g_RadarCommand = nullptr;
        HudToggleCommandRegistry* g_ChatCommand = nullptr;

        CommandRegistrationResult RegisterCommand(HudToggleCommandRegistry*& registry,
            NativeCommandMetadata metadata, const GameSymbols& symbols,
            HudToggleCallback callback, std::string& outError)
        {
            if (!callback)
            {
                outError = "HUD toggle callback is null";
                return CommandRegistrationResult::Failed;
            }
            if (!registry) registry = new HudToggleCommandRegistry(metadata, callback);
            return registry->TryRegister(symbols, outError);
        }
    }

    CommandRegistrationResult TryRegisterBeaconIndicatorToggleCommand(
        const GameSymbols& symbols, HudToggleCallback callback, std::string& outError)
    {
        return RegisterCommand(g_BeaconCommand,
            {"RABeaconIndicators", L"显示信标箭头", command_categories::DISPLAY_AND_TOOLS,
             L"开启或关闭队友信标的屏幕边缘方向提示"}, symbols, callback, outError);
    }

    CommandRegistrationResult TryRegisterRadarFeedToggleCommand(
        const GameSymbols& symbols, HudToggleCallback callback, std::string& outError)
    {
        return RegisterCommand(g_RadarCommand,
            {"RARadarFeed", L"雷达消息侧显", command_categories::DISPLAY_AND_TOOLS,
             L"开启或关闭可点击定位、保留重要事件的雷达历史清单"}, symbols, callback, outError);
    }

    CommandRegistrationResult TryRegisterChatPanelToggleCommand(
        const GameSymbols& symbols, HudToggleCallback callback, std::string& outError)
    {
        return RegisterCommand(g_ChatCommand,
            {"RAChatPanel", L"左下角聊天栏", command_categories::DISPLAY_AND_TOOLS,
             L"开启或关闭左下角聊天栏；关闭后恢复原生聊天"}, symbols, callback, outError);
    }

    void DisableBeaconIndicatorToggleCommand() { if (g_BeaconCommand) g_BeaconCommand->Disable(); }
    void DisableRadarFeedToggleCommand() { if (g_RadarCommand) g_RadarCommand->Disable(); }
    void DisableChatPanelToggleCommand() { if (g_ChatCommand) g_ChatCommand->Disable(); }
}
