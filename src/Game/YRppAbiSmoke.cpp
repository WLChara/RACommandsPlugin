#include "Commands/NativeCommandRegistry.h"

#include <YRPPCore.h>
#include <CommandClass.h>
#include <FootClass.h>
#include <Networking.h>

#include <type_traits>

static_assert(sizeof(void*) == 4);
static_assert(std::is_polymorphic_v<CommandClass>);
static_assert(std::is_polymorphic_v<TechnoClass>);
static_assert(std::is_base_of_v<CommandClass, ra_commands::game::NativeCommandRegistry>);
