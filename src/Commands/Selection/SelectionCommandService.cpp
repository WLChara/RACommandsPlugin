#include "Commands/Selection/SelectionCommandService.h"

#include <unordered_set>
#include <utility>

namespace ra_commands::selection
{
    namespace
    {
        FillState GetFillState(std::size_t phase)
        {
            switch (phase % 3)
            {
            case 0: return FillState::Full;
            case 1: return FillState::NotFull;
            default: return FillState::Empty;
            }
        }
    }

    SelectionCommandService::SelectionCommandService(ISelectionGamePort& game)
        : mGame(game)
    {
    }

    void SelectionCommandService::OnGameFrame()
    {
        (void)ObserveSelection();
    }

    void SelectionCommandService::OnIfvHotkey(
        bool secondPress, std::uint64_t systemDoubleClickTimeMs)
    {
        if (!ObserveSelection())
        {
            return;
        }

        const auto nowMs = mGame.GetMonotonicTimeMs();
        const bool withinDoubleClickTime = mIfvFirstPressMs &&
            systemDoubleClickTimeMs > 0 && nowMs >= *mIfvFirstPressMs &&
            nowMs - *mIfvFirstPressMs <= systemDoubleClickTimeMs;

        if (secondPress && withinDoubleClickTime)
        {
            const auto seeds = std::move(mIfvSeeds);
            ResetIfvState();
            ApplySelection(FilterSeedIfvModes(
                seeds, mGame.CaptureCandidates(SelectionScope::WholeMap)), true);
            return;
        }

        if (!secondPress && withinDoubleClickTime)
        {
            // 未确认释放的新按下沿不能刷新首按时间或种子，避免短时自动重复污染双击。
            return;
        }

        ResetCommandState();
        ResetIfvState();
        if (mCurrentMembers.empty())
        {
            return;
        }

        mIfvSeeds = mCurrentMembers;
        mIfvFirstPressMs = nowMs;
        ApplySelection(FilterSeedIfvModes(
            mIfvSeeds, mGame.CaptureCandidates(SelectionScope::Viewport)), true);
    }

    void SelectionCommandService::OnMindControlHotkey()
    {
        if (!PrepareCommand(ActiveCommand::MindControl))
        {
            return;
        }
        const auto members = mGame.CaptureMembersById(GetIds(mOriginalMembers));
        ApplySelection(FilterMindControlled(members, mNextPhase == 0), false);
        mNextPhase = (mNextPhase + 1) % 2;
    }

    void SelectionCommandService::OnKindHotkey()
    {
        if (!PrepareCommand(ActiveCommand::Kind))
        {
            return;
        }
        const auto members = mGame.CaptureMembersById(GetIds(mOriginalMembers));
        ApplySelection(FilterKind(members, mNextKind), false);
        mNextKind = NextKind(mNextKind);
    }

    void SelectionCommandService::OnAmmoHotkey()
    {
        if (!PrepareCommand(ActiveCommand::Ammo))
        {
            return;
        }
        const auto members = mGame.CaptureMembersById(GetIds(mOriginalMembers));
        ApplySelection(FilterAmmo(members, GetFillState(mNextPhase)), false);
        mNextPhase = (mNextPhase + 1) % 3;
    }

    void SelectionCommandService::OnPassengersHotkey()
    {
        if (!PrepareCommand(ActiveCommand::Passengers))
        {
            return;
        }
        const auto members = mGame.CaptureMembersById(GetIds(mOriginalMembers));
        ApplySelection(FilterPassengers(members, GetFillState(mNextPhase)), false);
        mNextPhase = (mNextPhase + 1) % 3;
    }

    void SelectionCommandService::OnCycleHotkey()
    {
        if (!PrepareCommand(ActiveCommand::Cycle))
        {
            return;
        }

        const auto nextId = mCycle.Next(FilterValidIds(GetIds(mOriginalMembers)));
        if (nextId)
        {
            ApplySelection({*nextId}, false);
        }
    }

    bool SelectionCommandService::Undo()
    {
        if (!ObserveSelection() || mUndoDepth >= mHistory.Size())
        {
            return false;
        }

        const auto restored = mHistory.Restore(mUndoDepth, [this](SelectionId id)
        {
            return mGame.IsSelectable(id);
        });
        ResetCommandState();
        ResetIfvState();
        ApplySelection(*restored, false, false);
        ++mUndoDepth;
        return true;
    }

    void SelectionCommandService::Reset()
    {
        mHistory.Clear();
        mObservedIds.clear();
        mCurrentMembers.clear();
        mSessionIdentity = 0;
        mLastFrame = 0;
        mUndoDepth = 0;
        mIsApplying = false;
        ResetCommandState();
        ResetIfvState();
    }

    bool SelectionCommandService::ObserveSelection()
    {
        if (mIsApplying)
        {
            return false;
        }

        const auto sessionIdentity = mGame.GetSessionIdentity();
        if (sessionIdentity == 0)
        {
            Reset();
            return false;
        }

        const auto frame = mGame.GetGameFrame();
        if (sessionIdentity != mSessionIdentity || frame < mLastFrame)
        {
            // 对局更替只建立当前选区基线，不能把上一局的选区记入历史。
            Reset();
            mSessionIdentity = sessionIdentity;
            mLastFrame = frame;
            mCurrentMembers = mGame.CaptureSelectedMembers();
            mObservedIds = mGame.CaptureSelectedIds();
            return true;
        }

        mLastFrame = frame;
        auto currentMembers = mGame.CaptureSelectedMembers();
        auto currentIds = mGame.CaptureSelectedIds();
        if (!SameSelection(currentIds, mObservedIds))
        {
            SavePreviousSelection();
            mObservedIds = std::move(currentIds);
            ResetCommandState();
            ResetIfvState();
        }
        mCurrentMembers = std::move(currentMembers);
        return true;
    }

    bool SelectionCommandService::PrepareCommand(ActiveCommand command)
    {
        if (!ObserveSelection())
        {
            return false;
        }

        ResetIfvState();
        if (mActiveCommand != command)
        {
            ResetCommandState();
            mActiveCommand = command;
            // 后续阶段始终从首次按键前的值型母集筛选，不能从上次筛选结果继续缩小。
            mOriginalMembers = mCurrentMembers;
        }
        return !mOriginalMembers.empty();
    }

    void SelectionCommandService::ApplySelection(
        const SelectionIds& ids, bool append, bool recordHistory)
    {
        const auto validIds = FilterValidIds(ids);
        mIsApplying = true;
        SelectionIds actualIds;
        std::vector<SelectionMember> actualMembers;
        try
        {
            actualIds = mGame.Apply(validIds, append);
            actualMembers = mGame.CaptureSelectedMembers();
        }
        catch (...)
        {
            mIsApplying = false;
            throw;
        }
        mIsApplying = false;

        if (!SameSelection(actualIds, mObservedIds) && recordHistory)
        {
            SavePreviousSelection();
        }
        mObservedIds = std::move(actualIds);
        mCurrentMembers = std::move(actualMembers);
    }

    void SelectionCommandService::SavePreviousSelection()
    {
        if (mUndoDepth != 0)
        {
            // 撤销后发生新编辑时，只保留当前位置之前的快照，丢弃已撤销的分支。
            SelectionHistory retained;
            for (std::size_t index = mHistory.Size(); index-- > mUndoDepth;)
            {
                retained.Save(*mHistory.Restore(index, [](SelectionId) { return true; }));
            }
            mHistory = std::move(retained);
            mUndoDepth = 0;
        }
        mHistory.Save(mObservedIds);
    }

    void SelectionCommandService::ResetCommandState()
    {
        mActiveCommand = ActiveCommand::None;
        mOriginalMembers.clear();
        mNextPhase = 0;
        mNextKind = SelectionKind::Unit;
        mCycle.Reset();
    }

    void SelectionCommandService::ResetIfvState()
    {
        mIfvSeeds.clear();
        mIfvFirstPressMs.reset();
    }

    SelectionIds SelectionCommandService::FilterValidIds(const SelectionIds& ids) const
    {
        SelectionIds validIds;
        validIds.reserve(ids.size());
        for (const auto id : ids)
        {
            if (mGame.IsSelectable(id))
            {
                validIds.push_back(id);
            }
        }
        return validIds;
    }

    SelectionIds SelectionCommandService::GetIds(const std::vector<SelectionMember>& members)
    {
        SelectionIds ids;
        ids.reserve(members.size());
        for (const auto& member : members)
        {
            ids.push_back(member.Id);
        }
        return ids;
    }

    bool SelectionCommandService::SameSelection(
        const SelectionIds& left, const SelectionIds& right)
    {
        return left.size() == right.size() &&
            std::unordered_set<SelectionId>(left.begin(), left.end()) ==
            std::unordered_set<SelectionId>(right.begin(), right.end());
    }
}
