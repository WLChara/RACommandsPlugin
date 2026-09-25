#include "Commands/SafeModeToggleCommand/SafeModeToggleCommandService.h"

#include <atomic>
#include <stdexcept>

void RunSafeModeToggleTests()
{
    std::atomic<bool> isEnabled{false};
    ra_commands::safe_mode::SafeModeToggleCommandService service(isEnabled);

    service.OnGameFrame(true, 1);
    if (!service.OnHotkey(true) || !isEnabled.load())
    {
        throw std::runtime_error("first safe-mode hotkey must enable the mode");
    }
    if (service.OnHotkey(true) || isEnabled.load())
    {
        throw std::runtime_error("second safe-mode hotkey must disable the mode");
    }

    (void)service.OnHotkey(true);
    service.OnGameFrame(true, 2);
    if (isEnabled.load())
    {
        throw std::runtime_error("new match epoch must reset safe mode");
    }
    if (service.OnHotkey(false) || isEnabled.load())
    {
        throw std::runtime_error("safe mode must not toggle outside a match");
    }
    (void)service.OnHotkey(true);
    service.OnGameFrame(false, 0);
    if (isEnabled.load())
    {
        throw std::runtime_error("leaving a match must reset safe mode");
    }
}
