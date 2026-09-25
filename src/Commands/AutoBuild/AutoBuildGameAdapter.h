#pragma once

#include "Commands/AutoBuild/IAutoBuildGamePort.h"

namespace ra_commands::game
{
    class NativeNetworkEventAdapter;

    class AutoBuildGameAdapter final : public auto_build::IAutoBuildGamePort
    {
    public:
        explicit AutoBuildGameAdapter(NativeNetworkEventAdapter& events);

        [[nodiscard]] bool IsMatchReady() const override;
        [[nodiscard]] std::uintptr_t GetSessionIdentity() const override;
        [[nodiscard]] std::uint32_t GetCurrentFrame() const override;
        [[nodiscard]] bool CaptureSlot(auto_build::BuildSlot slot,
            auto_build::SlotSnapshot& outSnapshot) const override;
        [[nodiscard]] bool TryEnqueueProduce(auto_build::BuildSlot slot,
            const auto_build::ProductId& product) const override;

    private:
        NativeNetworkEventAdapter& mEvents;
    };
}
