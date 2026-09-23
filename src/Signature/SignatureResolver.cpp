#include "Signature/SignatureResolver.h"

#include "Signature/OperandDecoder.h"
#include "Memory/ProcessMemory.h"
#include "Signature/SignatureScanner.h"

#include <vector>

namespace ra_commands::signatures
{
    bool TryResolve(
        const memory::PeImage& image,
        const SignatureSpec& spec,
        std::uintptr_t& outAddress,
        std::string& outError)
    {
        const auto result = memory::ScanUnique(image, spec.Section, spec.Pattern);
        if (result.Error != memory::ScanError::None)
        {
            outError = std::string(spec.Name) + ": " + memory::GetScanErrorText(result.Error);
            return false;
        }

        if (spec.Kind == AddressKind::Match)
        {
            outAddress = result.Address;
            return true;
        }

        if (spec.Kind == AddressKind::Absolute32Operand && spec.InstructionSize != 0)
        {
            std::vector<std::byte> instruction(spec.InstructionSize);
            if (memory::TryReadMemory(result.Address, instruction.data(), instruction.size()) &&
                memory::TryDecodeAbsolute32(instruction, spec.OperandOffset, outAddress))
            {
                return true;
            }
        }

        outError = std::string(spec.Name) + ": cannot decode target global operand";
        return false;
    }
}
