#include "Signature/SignatureScanner.h"

#include <libhat/scanner.hpp>

namespace ra_commands::memory
{
    ScanResult ScanUnique(
        const PeImage& image,
        std::string_view sectionName,
        std::string_view signature)
    {
        if (!image.IsValid())
        {
            return { 0, ScanError::InvalidModule };
        }

        const auto section = image.FindSection(sectionName);
        if (!section.Begin || !section.End)
        {
            return { 0, ScanError::SectionMissing };
        }

        const auto parsed = hat::parse_signature(signature);
        if (!parsed.has_value())
        {
            return { 0, ScanError::InvalidSignature };
        }

        const auto first = hat::find_pattern(section.Begin, section.End, parsed.value());
        if (!first.has_result())
        {
            return { 0, ScanError::NoMatch };
        }

        const auto* next = first.get() + 1;
        if (next < section.End &&
            hat::find_pattern(next, section.End, parsed.value()).has_result())
        {
            return { 0, ScanError::MultipleMatches };
        }

        return { reinterpret_cast<std::uintptr_t>(first.get()), ScanError::None };
    }

    const char* GetScanErrorText(ScanError error)
    {
        switch (error)
        {
        case ScanError::None: return "none";
        case ScanError::InvalidModule: return "invalid module";
        case ScanError::SectionMissing: return "section missing";
        case ScanError::InvalidSignature: return "invalid signature";
        case ScanError::NoMatch: return "signature not found";
        case ScanError::MultipleMatches: return "signature is ambiguous";
        }
        return "unknown scan error";
    }
}
