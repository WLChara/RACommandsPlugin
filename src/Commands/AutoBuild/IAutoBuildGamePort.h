#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ra_commands::auto_build
{
    enum class BuildSlot
    {
        Main,
        Defense
    };

    struct ProductId
    {
        std::int32_t TypeIndex = -1;
        std::string RegisteredName;
        bool IsNaval = false;

        [[nodiscard]] bool operator==(const ProductId&) const = default;
    };

    struct SlotSnapshot
    {
        std::optional<ProductId> Product;
        bool IsCombat = false;
        int BuildLimit = 0;
        bool IsReady = false;
        bool IsInProgress = false;
        bool IsManuallyStopped = false;
        bool IsEmpty = true;
    };

    /** 只传递当前帧的值；发送前由游戏侧重新验证工厂和产品。 */
    class IAutoBuildGamePort
    {
    public:
        virtual ~IAutoBuildGamePort() = default;

        [[nodiscard]] virtual bool IsMatchReady() const = 0;
        [[nodiscard]] virtual std::uintptr_t GetSessionIdentity() const = 0;
        [[nodiscard]] virtual std::uint32_t GetCurrentFrame() const = 0;
        [[nodiscard]] virtual bool CaptureSlot(BuildSlot slot,
            SlotSnapshot& outSnapshot) const = 0;
        /** true 仅表示 Produce 已写入原生 OutList。 */
        [[nodiscard]] virtual bool TryEnqueueProduce(BuildSlot slot,
            const ProductId& product) const = 0;
    };
}
