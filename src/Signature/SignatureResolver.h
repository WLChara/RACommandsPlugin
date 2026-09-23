#pragma once

#include "Signature/PeImage.h"
#include "Signature/SignatureSpec.h"

#include <cstdint>
#include <string>

namespace ra_commands::signatures
{
    bool TryResolve(
        const memory::PeImage& image,
        const SignatureSpec& spec,
        std::uintptr_t& outAddress,
        std::string& outError);
}
