#pragma once

#include "Commands/Selection/SelectionCore.h"

#include <cstdint>
#include <vector>

namespace ra_commands::selection
{
    enum class SelectionScope
    {
        Viewport,
        WholeMap
    };

    /**
     * 选择命令所需的游戏边界；实现和服务都只在游戏主线程调用。
     * 返回值必须是独立的值快照，不能夹带跨帧可解引用的游戏指针。
     */
    class ISelectionGamePort
    {
    public:
        virtual ~ISelectionGamePort() = default;

        /** 返回当前选区内可供命令处理的成员，ID 唯一且顺序稳定。 */
        [[nodiscard]] virtual std::vector<SelectionMember> CaptureSelectedMembers() const = 0;
        /** 包括建筑物在内的完整本地选区，用于撤销；筛选母集仍由上面的成员列表提供。 */
        [[nodiscard]] virtual SelectionIds CaptureSelectedIds() const = 0;
        /** 按稳定 ID 列表重新读取实时属性；失效对象不返回。 */
        [[nodiscard]] virtual std::vector<SelectionMember> CaptureMembersById(
            const SelectionIds& ids) const = 0;
        /** 返回指定范围内当前可选的候选成员，ID 唯一。 */
        [[nodiscard]] virtual std::vector<SelectionMember> CaptureCandidates(
            SelectionScope scope) const = 0;

        /**
         * 提交前须复验身份及可选状态。append 为 false 时替换选区，包括空列表清空选区。
         * 返回操作后完整选区的 ID，包括仍被选中的建筑物。
         */
        [[nodiscard]] virtual SelectionIds Apply(const SelectionIds& ids, bool append) = 0;
        /** 按稳定 ID 复验对象身份、当前归属、存活及可选状态。 */
        [[nodiscard]] virtual bool IsSelectable(SelectionId id) const = 0;

        /** 无对局时返回 0；同一身份不得跨两局复用，或在新局开始时使游戏帧回退。 */
        [[nodiscard]] virtual std::uint64_t GetSessionIdentity() const = 0;
        [[nodiscard]] virtual std::uint64_t GetGameFrame() const = 0;
        /** 单调毫秒时间只用于 IFV 双击时限；系统双击时限由调用方提供。 */
        [[nodiscard]] virtual std::uint64_t GetMonotonicTimeMs() const = 0;
    };
}
