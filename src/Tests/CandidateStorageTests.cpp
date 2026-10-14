#include "SelectionCapacity/CandidateStorage.h"

#include <stdexcept>
#define REQUIRE(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while(false)
#include <iostream>
#include <vector>

namespace
{
    using ra_commands::selection_capacity::CandidateEntry;
    using ra_commands::selection_capacity::CandidateStorage;
    using PublishResult = CandidateStorage::PublishResult;

    struct Publication
    {
        CandidateEntry* mBuffer;
        std::size_t mCapacity;
        PublishResult mResult = PublishResult::Completed;
        CandidateEntry* mAttempted = nullptr;
        int mCalls = 0;
    };

    PublishResult Publish(void* context, CandidateEntry* entries, std::size_t capacity)
    {
        auto& publication = *static_cast<Publication*>(context);
        REQUIRE(entries != publication.mBuffer && capacity > publication.mCapacity);
        // 发布之前旧表仍须完整可读，尾部新增空间必须为零。
        REQUIRE(entries[0].mObject == publication.mBuffer[0].mObject);
        REQUIRE(entries[capacity - 1].mObject == 0);
        publication.mAttempted = entries;
        ++publication.mCalls;
        if (publication.mResult == PublishResult::Completed)
        {
            publication.mBuffer = entries;
            publication.mCapacity = capacity;
        }
        return publication.mResult;
    }

    void TestGrowthAndPayload()
    {
        std::vector<CandidateEntry> fixed(4096);
        CandidateStorage storage;
        Publication publication{fixed.data(), fixed.size()};
        for (std::size_t i = 0; i < 20000; ++i)
        {
            REQUIRE(storage.TryReserve(i + 1, publication.mBuffer, i,
                publication.mCapacity, Publish, &publication));
            publication.mBuffer[i] = {static_cast<std::uint32_t>(i + 1),
                static_cast<int>(2 * i), -static_cast<int>(i)};
        }
        REQUIRE(publication.mCapacity == 32768 && publication.mCalls == 3);
        for (int i = 0; i < 20000; ++i)
        {
            REQUIRE(publication.mBuffer[i].mObject == static_cast<std::uint32_t>(i + 1));
            REQUIRE(publication.mBuffer[i].mX == 2 * i && publication.mBuffer[i].mY == -i);
        }
        for (std::size_t i = 20000; i < publication.mCapacity; ++i)
            REQUIRE(publication.mBuffer[i].mObject == 0);
        // 模拟原生引用先切回联机固定表，再释放动态表。
        publication.mBuffer = fixed.data();
        publication.mCapacity = fixed.size();
        storage.Reset();
        REQUIRE(!storage.Buffer() && storage.Capacity() == 0);
    }

    void TestRejectedPublicationAndRetry()
    {
        std::vector<CandidateEntry> fixed(4096);
        fixed[0] = {77, 4, 5};
        CandidateStorage storage;
        Publication publication{fixed.data(), fixed.size(), PublishResult::Rejected};
        REQUIRE(!storage.TryReserve(4097, fixed.data(), 4096, 4096, Publish, &publication));
        REQUIRE(!storage.Buffer() && publication.mBuffer == fixed.data() && fixed[0].mObject == 77);
        publication.mResult = PublishResult::Completed;
        REQUIRE(storage.TryReserve(4097, fixed.data(), 4096, 4096, Publish, &publication));
        REQUIRE(storage.Buffer()[0].mObject == 77 && storage.Capacity() == 8192);
        auto* oldBuffer = publication.mBuffer;
        publication.mResult = PublishResult::Rejected;
        REQUIRE(!storage.TryReserve(8193, oldBuffer, 8192, 8192, Publish, &publication));
        REQUIRE(storage.Buffer() == oldBuffer && oldBuffer[0].mObject == 77);
    }

    void TestIncompleteRollbackRetention()
    {
        std::vector<CandidateEntry> fixed(4096);
        fixed[0] = {123, 6, 7};
        CandidateStorage storage;
        Publication publication{fixed.data(), fixed.size()};
        REQUIRE(storage.TryReserve(4097, fixed.data(), 4096, 4096, Publish, &publication));
        const auto* oldBuffer = storage.Buffer();
        publication.mResult = PublishResult::Uncertain;
        REQUIRE(!storage.TryReserve(8193, oldBuffer, 8192, 8192, Publish, &publication));
        // 原生可能同时引用新旧表，两者都不可在失败返回时被释放。
        REQUIRE(oldBuffer[0].mObject == 123 && publication.mAttempted[0].mObject == 123);
        REQUIRE(storage.Buffer() == oldBuffer && storage.Capacity() == 8192);
        const int calls = publication.mCalls;
        REQUIRE(!storage.TryReserve(8193, oldBuffer, 8192, 8192, Publish, &publication));
        REQUIRE(publication.mCalls == calls);
        storage.Reset();
    }

    void TestInvalidSizeIsRejectedBeforePublication()
    {
        std::vector<CandidateEntry> fixed(4096);
        CandidateStorage storage;
        Publication publication{fixed.data(), fixed.size()};
        REQUIRE(!storage.TryReserve(CandidateStorage::MAX_CAPACITY + 1, fixed.data(),
            4096, 4096, Publish, &publication));
        REQUIRE(!storage.TryReserve(8192, fixed.data(), 4097, 4096, Publish, &publication));
        REQUIRE(publication.mCalls == 0 && !storage.Buffer());
    }
}

void RunCandidateStorageTests()
{
    TestGrowthAndPayload();
    TestRejectedPublicationAndRetry();
    TestIncompleteRollbackRetention();
    TestInvalidSizeIsRejectedBeforePublication();
    std::cout << "CandidateStorage tests passed\n";
}

#ifdef RA_COMMANDS_CANDIDATE_STORAGE_STANDALONE_TEST
int main()
{
    try { RunCandidateStorageTests(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
#endif