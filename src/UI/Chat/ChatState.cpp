#include "UI/Chat/ChatState.h"

#include <algorithm>
#include <utility>

namespace ra_commands::ui::chat
{
    namespace
    {
        bool IsHigh(wchar_t character) { return character >= 0xD800 && character <= 0xDBFF; }
        bool IsLow(wchar_t character) { return character >= 0xDC00 && character <= 0xDFFF; }
        std::size_t Previous(std::wstring_view text, std::size_t position)
        {
            if (!position) return 0;
            --position;
            return position && IsLow(text[position]) && IsHigh(text[position - 1]) ? position - 1 : position;
        }
        std::size_t Next(std::wstring_view text, std::size_t position)
        {
            if (position >= text.size()) return text.size();
            return IsHigh(text[position]) && position + 1 < text.size() && IsLow(text[position + 1]) ? position + 2 : position + 1;
        }
        std::wstring Sanitize(std::wstring_view text, std::size_t capacity)
        {
            std::wstring output;
            output.reserve((std::min)(text.size(), capacity));
            bool previousCr = false;
            for (std::size_t index = 0; index < text.size() && output.size() < capacity; ++index)
            {
                const auto character = text[index];
                if (character == L'\r' || character == L'\n' || character == L'\t')
                {
                    if (!(character == L'\n' && previousCr)) output += L' ';
                    previousCr = character == L'\r';
                    continue;
                }
                previousCr = false;
                if (character < 32 || (character >= 127 && character <= 159) || IsLow(character)) continue;
                if (IsHigh(character))
                {
                    if (index + 1 >= text.size() || !IsLow(text[index + 1])) continue;
                    if (capacity - output.size() < 2) break;
                    output += character;
                    output += text[++index];
                }
                else output += character;
            }
            return output;
        }
    }

    void ChatState::Begin(std::wstring channel)
    {
        End();
        mChannel = std::move(channel);
        mEditing = true;
    }

    void ChatState::End() noexcept
    {
        mEditing = false;
        mDraft.clear();
        mComposition.clear();
        mStatus.clear();
        mChannel.clear();
        mCaret = mAnchor = 0;
        ResetRecallNavigation();
    }

    void ChatState::Reset() noexcept
    {
        End();
        mMessages.clear();
        mSubmittedDrafts.clear();
    }

    void ChatState::ResetRecallNavigation() noexcept
    {
        mRecallIndex.reset();
        mRecallDraft.clear();
    }

    void ChatState::ApplyRecalledDraft(std::wstring draft)
    {
        mDraft = std::move(draft);
        mCaret = mAnchor = mDraft.size();
        mComposition.clear();
        mStatus.clear();
    }

    void ChatState::RecordSubmittedDraft(std::wstring_view draft)
    {
        if (draft.empty()) return;
        auto submitted = Sanitize(draft, MAX_DRAFT_UNITS);
        // 历史保存实际提交原文，不能把截断或修复过的字符串误记为原命令。
        if (submitted != draft) return;
        if (mSubmittedDrafts.empty() || mSubmittedDrafts.back() != submitted)
        {
            mSubmittedDrafts.push_back(std::move(submitted));
            if (mSubmittedDrafts.size() > MAX_SUBMITTED_DRAFTS) mSubmittedDrafts.erase(mSubmittedDrafts.begin());
        }
        ResetRecallNavigation();
    }

    bool ChatState::RecallPreviousDraft()
    {
        if (!mEditing || HasComposition() || mSubmittedDrafts.empty() || (mRecallIndex && *mRecallIndex == 0)) return false;
        const auto nextIndex = mRecallIndex ? *mRecallIndex - 1 : mSubmittedDrafts.size() - 1;
        auto recalled = mSubmittedDrafts[nextIndex];
        if (!mRecallIndex) mRecallDraft = mDraft;
        ApplyRecalledDraft(std::move(recalled));
        mRecallIndex = nextIndex;
        return true;
    }

    bool ChatState::RecallNextDraft()
    {
        if (!mEditing || HasComposition() || !mRecallIndex) return false;
        const auto nextIndex = *mRecallIndex + 1;
        if (nextIndex < mSubmittedDrafts.size())
        {
            ApplyRecalledDraft(mSubmittedDrafts[nextIndex]);
            mRecallIndex = nextIndex;
        }
        else
        {
            ApplyRecalledDraft(mRecallDraft);
            ResetRecallNavigation();
        }
        return true;
    }

    bool ChatState::EraseSelection()
    {
        const auto start = (std::min)(mCaret, mAnchor);
        const auto end = (std::max)(mCaret, mAnchor);
        if (start == end) return false;
        mDraft.erase(start, end - start);
        mCaret = mAnchor = start;
        return true;
    }

    bool ChatState::Insert(std::wstring_view text)
    {
        if (!mEditing) return false;
        const auto selected = (std::max)(mCaret, mAnchor) - (std::min)(mCaret, mAnchor);
        auto clean = Sanitize(text, MAX_DRAFT_UNITS - (mDraft.size() - selected));
        if (clean.empty()) return false;
        EraseSelection();
        mDraft.insert(mCaret, clean);
        mCaret += clean.size();
        mAnchor = mCaret;
        mStatus.clear();
        return true;
    }

    bool ChatState::Backspace()
    {
        if (!mEditing) return false;
        if (EraseSelection()) return true;
        if (!mCaret) return false;
        const auto start = Previous(mDraft, mCaret);
        mDraft.erase(start, mCaret - start);
        mCaret = mAnchor = start;
        return true;
    }

    bool ChatState::Delete()
    {
        if (!mEditing) return false;
        if (EraseSelection()) return true;
        if (mCaret >= mDraft.size()) return false;
        mDraft.erase(mCaret, Next(mDraft, mCaret) - mCaret);
        mAnchor = mCaret;
        return true;
    }

    void ChatState::Move(int direction, bool extendSelection)
    {
        if (!mEditing) return;
        if (!extendSelection && mCaret != mAnchor)
            mCaret = direction < 0 ? (std::min)(mCaret, mAnchor) : (std::max)(mCaret, mAnchor);
        else mCaret = direction < 0 ? Previous(mDraft, mCaret) : Next(mDraft, mCaret);
        if (!extendSelection) mAnchor = mCaret;
    }

    void ChatState::MoveToEnd(bool end, bool extendSelection)
    {
        if (!mEditing) return;
        mCaret = end ? mDraft.size() : 0;
        if (!extendSelection) mAnchor = mCaret;
    }

    void ChatState::SelectAll() noexcept
    {
        if (mEditing) { mAnchor = 0; mCaret = mDraft.size(); }
    }

    void ChatState::SetCaret(std::size_t position, bool extendSelection) noexcept
    {
        if (!mEditing) return;
        mCaret = (std::min)(position, mDraft.size());
        if (mCaret && mCaret < mDraft.size() && IsLow(mDraft[mCaret]) && IsHigh(mDraft[mCaret - 1])) --mCaret;
        if (!extendSelection) mAnchor = mCaret;
    }

    std::wstring ChatState::CopySelection() const
    {
        const auto start = (std::min)(mCaret, mAnchor);
        return mDraft.substr(start, (std::max)(mCaret, mAnchor) - start);
    }

    void ChatState::SetComposition(std::wstring text) { mComposition = std::move(text); }
    void ChatState::SetStatus(std::wstring text) { mStatus = std::move(text); }

    bool ChatState::Append(std::wstring sender, std::wstring text, HudColor color, std::uint64_t nowMs,
        int playerId, bool isLocal)
    {
        if (text.empty() || !mNextId) return false;
        // 先完成可能分配的操作再淘汰旧记录；失败时原生调用方仍可显示原消息。
        ChatMessage message{ mNextId, std::move(sender), std::move(text), color, nowMs, playerId, isLocal };
        mMessages.push_back(std::move(message));
        if (mMessages.size() > MAX_HISTORY_MESSAGES) mMessages.erase(mMessages.begin());
        ++mNextId;
        return true;
    }

    std::wstring ChatState::CopyMessage(std::uint64_t id, bool selfMask, std::wstring_view maskName) const
    {
        const auto found = std::find_if(mMessages.begin(), mMessages.end(), [id](const ChatMessage& message) { return message.mId == id; });
        return found == mMessages.end() ? std::wstring{} :
            L"<" + (selfMask && found->mIsLocal ? std::wstring{ maskName } : found->mSender) + L"> " + found->mText;
    }

    ChatView ChatState::Capture(bool isEnabled, bool selfMask, std::wstring_view maskName) const
    {
        ChatView view{ isEnabled, mEditing, mDraft, mCaret, mAnchor, mComposition, mChannel, mStatus, mMessages };
        if (selfMask)
            for (auto& message : view.mMessages)
                if (message.mIsLocal) message.mSender.assign(maskName);
        return view;
    }
}
