#include "Commands/AFloorCommand/AFloorCommandService.h"

#include <cstdint>
#include <stdexcept>

namespace
{
    using namespace ra_commands::a_floor;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    class FakeGame final : public IAFloorGamePort
    {
    public:
        bool Ready = true;
        std::uintptr_t Session = 1;
        std::uint32_t Frame = 100;

        bool IsMatchReady() const override { return Ready; }
        std::uintptr_t GetSessionIdentity() const override { return Session; }
        std::uint32_t GetCurrentFrame() const override { return Frame; }
    };

    void TestToggle()
    {
        FakeGame game;
        AFloorCommandService service(game);
        Require(!service.IsEnabled(), "A-floor mode must start disabled");
        service.OnHotkey();
        Require(service.IsEnabled(), "first hotkey must enable A-floor mode");
        service.OnHotkey();
        Require(!service.IsEnabled(), "second hotkey must disable A-floor mode");
        service.OnHotkey();
        Require(service.IsEnabled(), "third hotkey must enable A-floor mode again");
    }

    void TestSessionAndFrameReset()
    {
        FakeGame game;
        AFloorCommandService service(game);
        service.OnHotkey();
        game.Session = 2;
        ++game.Frame;
        service.OnGameFrame();
        Require(!service.IsEnabled(), "new session must disable the old mode");

        service.OnHotkey();
        Require(service.IsEnabled(), "new session may enable the mode independently");
        game.Frame = 1;
        service.OnGameFrame();
        Require(!service.IsEnabled(), "frame rewind must disable the old mode");
    }

    void TestUnavailableAndShutdownReset()
    {
        FakeGame game;
        AFloorCommandService service(game);
        game.Ready = false;
        service.OnHotkey();
        Require(!service.IsEnabled(), "hotkey outside a match must do nothing");
        game.Ready = true;
        game.Session = 0;
        service.OnHotkey();
        Require(!service.IsEnabled(), "missing session identity must prevent enabling");

        game.Session = 1;
        service.OnHotkey();
        game.Ready = false;
        service.OnGameFrame();
        Require(!service.IsEnabled(), "leaving the match must disable the mode");

        game.Ready = true;
        service.OnHotkey();
        service.Reset();
        Require(!service.IsEnabled(), "Shutdown reset must disable the mode");
    }
}

void RunAFloorTests()
{
    TestToggle();
    TestSessionAndFrameReset();
    TestUnavailableAndShutdownReset();
}
