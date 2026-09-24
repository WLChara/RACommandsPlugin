#include "Commands/RangeDisplayCommand/RangeDisplayCommandService.h"

#include <cstdint>
#include <stdexcept>

namespace
{
    using namespace ra_commands::range_display;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    class FakeGame final : public IRangeDisplayGamePort
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
        RangeDisplayCommandService service(game);
        Require(!service.IsEnabled(), "range display must start disabled");

        service.OnHotkey();
        Require(service.IsEnabled(), "first hotkey must enable range display");
        service.OnHotkey();
        Require(!service.IsEnabled(), "second hotkey must disable range display");
    }

    void TestSessionAndFrameReset()
    {
        FakeGame game;
        RangeDisplayCommandService service(game);
        service.OnHotkey();
        Require(service.IsEnabled(), "hotkey must enable range display in a match");

        game.Session = 2;
        ++game.Frame;
        service.OnGameFrame();
        Require(!service.IsEnabled(), "a new session must reset range display");

        service.OnHotkey();
        Require(service.IsEnabled(), "range display may be enabled in the new session");
        game.Frame = 1;
        service.OnGameFrame();
        Require(!service.IsEnabled(), "frame rollback must reset range display");
    }

    void TestLeavingMatchAndShutdownReset()
    {
        FakeGame game;
        RangeDisplayCommandService service(game);
        service.OnHotkey();
        game.Ready = false;
        service.OnGameFrame();
        Require(!service.IsEnabled(), "leaving a match must reset range display");

        game.Ready = true;
        service.OnHotkey();
        Require(service.IsEnabled(), "range display must be toggleable in a ready match");
        service.Reset();
        Require(!service.IsEnabled(), "Shutdown reset must disable range display");
    }

    void TestUnavailableMatch()
    {
        FakeGame game;
        RangeDisplayCommandService service(game);
        game.Ready = false;
        service.OnHotkey();
        Require(!service.IsEnabled(), "hotkey outside a match must do nothing");

        game.Ready = true;
        game.Session = 0;
        service.OnHotkey();
        Require(!service.IsEnabled(), "missing session identity must prevent enabling");
    }
}

void RunRangeDisplayTests()
{
    TestToggle();
    TestSessionAndFrameReset();
    TestLeavingMatchAndShutdownReset();
    TestUnavailableMatch();
}
