#include "Commands/BeaconClearCommand/BeaconRecipientProgress.h"

#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
}

void RunBeaconRecipientProgressTests()
{
    ra_commands::beacon_clear::BeaconRecipientProgress progress;
    Require(progress.Begin(0, 0x1000), "first beacon identity must begin progress");
    progress.MarkSubmitted(0, 0x2000);
    Require(progress.HasSubmitted(0, 0x2000),
        "successful first recipient must not be resent on retry");
    Require(progress.Begin(0, 0x1000) && progress.HasSubmitted(0, 0x2000),
        "same beacon retry must preserve per-recipient progress");
    Require(!progress.Begin(0, 0x3000) && !progress.HasSubmitted(0, 0x2000),
        "reused slot must drop old recipient progress and old delete intent");
    Require(progress.Begin(0, 0x3000),
        "new beacon may start a separate future deletion attempt");
    progress.MarkSubmitted(0, 0x4000);
    progress.ClearSlot(0);
    Require(!progress.HasSubmitted(0, 0x4000),
        "completed deletion must forget previous recipient progress");
    Require(progress.Begin(23, 0x5000), "last valid beacon slot must be accepted");
    Require(!progress.Begin(24, 0x5000) && !progress.Begin(0, 0),
        "invalid slot or null beacon identity must be rejected");
    progress.Reset();
    Require(progress.Begin(23, 0x6000),
        "session reset must allow a new beacon in prior occupied slot");
}
