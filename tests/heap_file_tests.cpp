#include <gtest/gtest.h>

#include "storage/heap_file.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/page.h"
#include "storage/page_directory.h"

/*
1. InsertAndGetSingleRecord
   - create HeapFile
   - insert one record
   - verify returned RID
   - call GetRecord
   - verify bytes match

2. InsertMultipleRecordsSamePage
   - insert several small records
   - verify they all land on the same page_id
   - verify different slot_ids
   - read all records back

3. InsertSpillsAcrossMultiplePages
   - insert records until one page fills
   - keep inserting
   - verify a later record gets a different page_id
   - verify records from both pages are readable

4. GetRecordSurvivesEvictionAndReload
   - insert records
   - use a very small buffer pool
   - force their pages out by touching other pages
   - call GetRecord again
   - verify data reloads correctly from disk

5. DeleteRecord
   - insert a record
   - delete via RID
   - verify DeleteRecord returns true
   - GetRecord(rid) returns nullopt

6. DeleteInvalidRID
   - invalid page_id
   - valid page but invalid slot_id
   - already-deleted RID
   - all should return false

7. UpdateRecordSameSize
   - insert record
   - update with same-size data
   - RID stays the same
   - GetRecord returns new bytes

8. UpdateRecordSmaller
   - insert larger record
   - update with smaller one
   - verify success and new data

9. UpdateRecordLargerWhenSpaceExists
   - insert record
   - update to larger record while page still has room
   - verify success and same RID

10. UpdateRecordFailsWhenPageLacksSpace
    - fill the page enough that growing one record cannot fit
    - update should return false
    - original record must remain unchanged

11. UpdateInvalidRID
    - nonexistent page
    - nonexistent slot
    - deleted slot
    - update should return false

12. OversizedRecordFails
    - construct a record larger than any HeapPage can store
    - InsertRecord returns nullopt
    - verify impossible records are rejected before normal insertion

13. ReturnedRecordOwnsItsData
    - insert record
    - call HeapFile::GetRecord
    - force eviction / overwrite frames
    - verify the returned OwnedRecord still contains the original data
    - proves the span-to-vector copy boundary is correct

14. DeletedSlotCanBeReused
    - insert A, B, C
    - delete B
    - insert D
    - verify insertion still works
    - verify the deleted slot can be reused

15. RecordsRemainIndependentAcrossPages
    - deliberately create at least two heap pages
    - modify/delete/update records on one page
    - verify records on the other page are untouched

16. ReopenHeapFilePreservesRecords
    - create HeapFile and insert a record
    - save the first directory page ID and RID
    - flush dirty pages to disk
    - destroy the original BufferPoolManager
    - reopen the same database file with a new BufferPoolManager
    - reopen HeapFile using the saved directory page ID
    - verify the old RID still retrieves the original record

17. InsertCreatesDirectoryEntryWithCorrectFreeSpace
    - insert the first record into a new HeapFile
    - verify the directory contains exactly one entry
    - verify the entry page_id matches the inserted record's page
    - verify cached directory free_space matches HeapPage::GetFreeSpace()

18. MultipleInsertsSamePageUpdateDirectoryFreeSpace
    - insert a record and record the directory free_space
    - insert another record that fits on the same data page
    - verify both records use the same page_id
    - verify the directory still contains only one entry
    - verify cached free_space decreases
    - verify cached free_space still matches the actual HeapPage

19. SpillToSecondPageAddsSecondDirectoryEntry
    - insert records large enough to force creation of a second data page
    - verify the records span different page_ids
    - verify the directory entry count increases to two
    - verify the directory contains entries for both data pages

20. DeleteRecordUpdatesDirectoryFreeSpace
    - insert a record
    - record the directory's cached free_space
    - delete the record
    - verify directory free_space increases
    - verify cached free_space matches the actual HeapPage after deletion

21. UpdateRecordLargerDecreasesDirectoryFreeSpace
    - insert a smaller record
    - record the directory's cached free_space
    - update the record to a larger value
    - verify the update succeeds
    - verify directory free_space decreases
    - verify cached free_space matches the actual HeapPage

22. UpdateRecordSmallerIncreasesDirectoryFreeSpace
    - insert a larger record
    - record the directory's cached free_space
    - update the record to a smaller value
    - verify the update succeeds
    - verify directory free_space increases
    - verify cached free_space matches the actual HeapPage

23. FailedUpdateDoesNotChangeDirectoryFreeSpace
    - arrange a page so a record cannot grow enough to satisfy an update
    - record the directory's cached free_space before the update
    - attempt the oversized update
    - verify UpdateRecord returns false
    - verify directory free_space remains unchanged
    - proves failed operations do not corrupt cached directory metadata
*/

class HeapFileTest : public ::testing::Test {
protected:
    fs::path db_path = "heap_file_test.db";

    std::unique_ptr<BufferPoolManager> bpm;

    void SetUp() override {
        if (fs::exists(db_path)) {
            fs::remove(db_path);
        }

        bpm = std::make_unique<BufferPoolManager>(
            2,
            db_path
        );
    }

    PageDirectoryEntry ReadDirectoryEntry(HeapFile& heap_file, std::uint16_t index) {
        int directory_page_id = heap_file.GetFirstDirectoryPageId();

        Page* dir_page = bpm->FetchPage(directory_page_id);
        if (dir_page == nullptr) {
            throw std::runtime_error("Failed to fetch directory page in test");
        }

        PageDirectoryEntry entry{};

        {
            auto guard = dir_page->ReadLatch();
            PageDirectory directory(*dir_page);
            entry = directory.ReadEntry(index);
        }

        bpm->UnpinPage(directory_page_id, false);
        return entry;
    }

    DirectoryPageHeader ReadDirectoryHeader(HeapFile& heap_file) {
        int directory_page_id = heap_file.GetFirstDirectoryPageId();

        Page* dir_page = bpm->FetchPage(directory_page_id);
        if (dir_page == nullptr) {
            throw std::runtime_error("Failed to fetch directory page in test");
        }

        DirectoryPageHeader header{};

        {
            auto guard = dir_page->ReadLatch();
            PageDirectory directory(*dir_page);
            header = directory.ReadHeader();
        }

        bpm->UnpinPage(directory_page_id, false);
        return header;
    }

    std::uint16_t ReadHeapPageFreeSpace(int page_id) {
        Page* page = bpm->FetchPage(page_id);
        if (page == nullptr) {
            throw std::runtime_error("Failed to fetch heap page in test");
        }

        std::uint16_t free_space = 0;

        {
            auto guard = page->ReadLatch();
            HeapPage heap_page(*page);
            free_space = heap_page.GetFreeSpace();
        }

        bpm->UnpinPage(page_id, false);
        return free_space;
    }

    void TearDown() override {
        bpm.reset();

        if (fs::exists(db_path)) {
            fs::remove(db_path);
        }
    }
};

TEST_F(HeapFileTest, InsertAndGetSingleRecord) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 4> data {
        std::byte{'A'},
        std::byte{'B'},
        std::byte{'C'},
        std::byte{'D'}
    };

    Record record {
        data.data(),
        data.size()
    };

    auto rid = heap_file.InsertRecord(record);

    ASSERT_TRUE(rid.has_value());

    auto stored = heap_file.GetRecord(*rid);

    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), data.size());

    EXPECT_TRUE(
        std::equal(
            stored->begin(),
            stored->end(),
            data.begin()
        )
    );
}

TEST_F(HeapFileTest, InsertMultipleRecordsSamePage) {
    HeapFile heap_file(*bpm);

    std::array<std::array<std::byte, 1>, 3> records{{
        {std::byte{'A'}},
        {std::byte{'B'}},
        {std::byte{'C'}}
    }};

    std::array<RID, 3> rids{};

    for (std::size_t i = 0; i < records.size(); ++i) {
        auto rid = heap_file.InsertRecord(records[i]);

        ASSERT_TRUE(rid.has_value());

        rids[i] = *rid;
    }

    for (std::size_t i = 1; i < rids.size(); ++i) {
        EXPECT_EQ(rids[i].page_id, rids[0].page_id);
        EXPECT_NE(rids[i].slot_id, rids[0].slot_id);
    }

    for (std::size_t i = 0; i < rids.size(); ++i) {
        auto stored = heap_file.GetRecord(rids[i]);

        ASSERT_TRUE(stored.has_value());
        ASSERT_EQ(stored->size(), records[i].size());

        EXPECT_TRUE(
            std::equal(
                stored->begin(),
                stored->end(),
                records[i].begin()
            )
        );
    }
}

TEST_F(HeapFileTest, InsertSpillsAcrossMultiplePages) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 1500> data1{};
    std::array<std::byte, 1500> data2{};
    std::array<std::byte, 1500> data3{};

    data1.fill(std::byte{'A'});
    data2.fill(std::byte{'B'});
    data3.fill(std::byte{'C'});

    auto rid1 = heap_file.InsertRecord(data1);
    auto rid2 = heap_file.InsertRecord(data2);
    auto rid3 = heap_file.InsertRecord(data3);

    ASSERT_TRUE(rid1.has_value());
    ASSERT_TRUE(rid2.has_value());
    ASSERT_TRUE(rid3.has_value());

    EXPECT_EQ(rid1->page_id, rid2->page_id);
    EXPECT_NE(rid3->page_id, rid1->page_id);

    auto rec1 = heap_file.GetRecord(*rid1);
    auto rec3 = heap_file.GetRecord(*rid3);

    ASSERT_TRUE(rec1.has_value());
    ASSERT_TRUE(rec3.has_value());

    EXPECT_TRUE(
        std::equal(
            rec1->begin(),
            rec1->end(),
            data1.begin()
        )
    );

    EXPECT_TRUE(
        std::equal(
            rec3->begin(),
            rec3->end(),
            data3.begin()
        )
    );
}

TEST_F(HeapFileTest, GetRecordSurvivesEvictionAndReload) {
    bpm.reset();

    bpm = std::make_unique<BufferPoolManager>(
        1,
        db_path
    );

    HeapFile heap_file(*bpm);

    std::array<std::byte, 2500> data1{};
    std::array<std::byte, 2500> data2{};

    data1.fill(std::byte{'A'});
    data2.fill(std::byte{'B'});

    auto rid1 = heap_file.InsertRecord(data1);
    auto rid2 = heap_file.InsertRecord(data2);

    ASSERT_TRUE(rid1.has_value());
    ASSERT_TRUE(rid2.has_value());

    ASSERT_NE(rid1->page_id, rid2->page_id);

    // With one frame, accessing page 2 should force page 1 out.
    auto rec2 = heap_file.GetRecord(*rid2);
    ASSERT_TRUE(rec2.has_value());

    auto rec1 = heap_file.GetRecord(*rid1);
    ASSERT_TRUE(rec1.has_value());

    EXPECT_TRUE(
        std::equal(
            rec1->begin(),
            rec1->end(),
            data1.begin()
        )
    );
}

TEST_F(HeapFileTest, DeleteRecord) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 4> data{
        std::byte{'A'},
        std::byte{'B'},
        std::byte{'C'},
        std::byte{'D'}
    };

    auto rid = heap_file.InsertRecord(data);

    ASSERT_TRUE(rid.has_value());

    auto before_delete = heap_file.GetRecord(*rid);
    ASSERT_TRUE(before_delete.has_value());

    EXPECT_TRUE(
        heap_file.DeleteRecord(*rid)
    );

    auto after_delete = heap_file.GetRecord(*rid);

    EXPECT_FALSE(after_delete.has_value());
}

TEST_F(HeapFileTest, DeleteInvalidRID) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 4> data{
        std::byte{'A'},
        std::byte{'B'},
        std::byte{'C'},
        std::byte{'D'}
    };

    auto rid = heap_file.InsertRecord(data);

    ASSERT_TRUE(rid.has_value());

    // Invalid page_id
    RID invalid_page{
        rid->page_id + 100,
        0
    };

    EXPECT_FALSE(
        heap_file.DeleteRecord(invalid_page)
    );

    // Valid page, invalid slot_id
    RID invalid_slot{
        rid->page_id,
        static_cast<std::uint16_t>(rid->slot_id + 100)
    };

    EXPECT_FALSE(
        heap_file.DeleteRecord(invalid_slot)
    );

    // Delete once successfully
    EXPECT_TRUE(
        heap_file.DeleteRecord(*rid)
    );

    // Deleting the same RID again should fail
    EXPECT_FALSE(
        heap_file.DeleteRecord(*rid)
    );
}

TEST_F(HeapFileTest, UpdateRecordSameSize) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 4> original{
        std::byte{'A'},
        std::byte{'B'},
        std::byte{'C'},
        std::byte{'D'}
    };

    std::array<std::byte, 4> updated{
        std::byte{'W'},
        std::byte{'X'},
        std::byte{'Y'},
        std::byte{'Z'}
    };

    auto rid = heap_file.InsertRecord(original);

    ASSERT_TRUE(rid.has_value());

    RID original_rid = *rid;

    EXPECT_TRUE(
        heap_file.UpdateRecord(
            *rid,
            updated
        )
    );

    // RID should still refer to the same logical record.
    EXPECT_EQ(rid->page_id, original_rid.page_id);
    EXPECT_EQ(rid->slot_id, original_rid.slot_id);

    auto stored = heap_file.GetRecord(*rid);

    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), updated.size());

    EXPECT_TRUE(
        std::equal(
            stored->begin(),
            stored->end(),
            updated.begin()
        )
    );
}

TEST_F(HeapFileTest, UpdateRecordSmaller) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 100> original{};
    original.fill(std::byte{'A'});

    std::array<std::byte, 20> updated{};
    updated.fill(std::byte{'B'});

    auto rid = heap_file.InsertRecord(original);

    ASSERT_TRUE(rid.has_value());

    EXPECT_TRUE(
        heap_file.UpdateRecord(
            *rid,
            updated
        )
    );

    auto stored = heap_file.GetRecord(*rid);

    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), updated.size());

    EXPECT_TRUE(
        std::equal(
            stored->begin(),
            stored->end(),
            updated.begin()
        )
    );
}

TEST_F(HeapFileTest, UpdateRecordLargerWhenSpaceExists) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 100> original{};
    original.fill(std::byte{'A'});

    std::array<std::byte, 500> updated{};
    updated.fill(std::byte{'B'});

    auto rid = heap_file.InsertRecord(original);

    ASSERT_TRUE(rid.has_value());

    RID original_rid = *rid;

    EXPECT_TRUE(
        heap_file.UpdateRecord(
            *rid,
            updated
        )
    );

    EXPECT_EQ(rid->page_id, original_rid.page_id);
    EXPECT_EQ(rid->slot_id, original_rid.slot_id);

    auto stored = heap_file.GetRecord(*rid);

    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), updated.size());

    EXPECT_TRUE(
        std::equal(
            stored->begin(),
            stored->end(),
            updated.begin()
        )
    );
}

TEST_F(HeapFileTest, UpdateRecordFailsWhenPageLacksSpace) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 500> original{};
    original.fill(std::byte{'A'});

    std::array<std::byte, 3000> filler{};
    filler.fill(std::byte{'B'});

    auto rid = heap_file.InsertRecord(original);
    auto filler_rid = heap_file.InsertRecord(filler);

    ASSERT_TRUE(rid.has_value());
    ASSERT_TRUE(filler_rid.has_value());

    // Make sure both records are actually on the same page.
    ASSERT_EQ(rid->page_id, filler_rid->page_id);

    std::array<std::byte, 2000> too_large{};
    too_large.fill(std::byte{'C'});

    EXPECT_FALSE(
        heap_file.UpdateRecord(
            *rid,
            too_large
        )
    );

    // Original record should still be intact.
    auto stored = heap_file.GetRecord(*rid);

    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), original.size());

    EXPECT_TRUE(
        std::equal(
            stored->begin(),
            stored->end(),
            original.begin()
        )
    );
}

TEST_F(HeapFileTest, UpdateInvalidRID) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 4> data{
        std::byte{'A'},
        std::byte{'B'},
        std::byte{'C'},
        std::byte{'D'}
    };

    std::array<std::byte, 4> updated{
        std::byte{'W'},
        std::byte{'X'},
        std::byte{'Y'},
        std::byte{'Z'}
    };

    auto rid = heap_file.InsertRecord(data);
    ASSERT_TRUE(rid.has_value());

    RID invalid_page{
        rid->page_id + 100,
        0
    };

    EXPECT_FALSE(
        heap_file.UpdateRecord(
            invalid_page,
            updated
        )
    );

    RID invalid_slot{
        rid->page_id,
        static_cast<std::uint16_t>(rid->slot_id + 100)
    };

    EXPECT_FALSE(
        heap_file.UpdateRecord(
            invalid_slot,
            updated
        )
    );

    ASSERT_TRUE(
        heap_file.DeleteRecord(*rid)
    );

    EXPECT_FALSE(
        heap_file.UpdateRecord(
            *rid,
            updated
        )
    );
}

TEST_F(HeapFileTest, OversizedRecordFails) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, PAGE_SIZE + 1> oversized{};
    oversized.fill(std::byte{'A'});

    auto rid = heap_file.InsertRecord(oversized);

    EXPECT_FALSE(rid.has_value());
}

TEST_F(HeapFileTest, ReturnedRecordOwnsItsData) {
    bpm.reset();

    bpm = std::make_unique<BufferPoolManager>(
        1,
        db_path
    );

    HeapFile heap_file(*bpm);

    std::array<std::byte, 2500> data1{};
    std::array<std::byte, 2500> data2{};

    data1.fill(std::byte{'A'});
    data2.fill(std::byte{'B'});

    auto rid1 = heap_file.InsertRecord(data1);
    auto rid2 = heap_file.InsertRecord(data2);

    ASSERT_TRUE(rid1.has_value());
    ASSERT_TRUE(rid2.has_value());
    ASSERT_NE(rid1->page_id, rid2->page_id);

    auto owned = heap_file.GetRecord(*rid1);

    ASSERT_TRUE(owned.has_value());

    // Force the single frame to contain another page.
    auto other = heap_file.GetRecord(*rid2);
    ASSERT_TRUE(other.has_value());

    // owned should still be valid because HeapFile copied it.
    ASSERT_EQ(owned->size(), data1.size());

    EXPECT_TRUE(
        std::equal(
            owned->begin(),
            owned->end(),
            data1.begin()
        )
    );
}

TEST_F(HeapFileTest, DeletedSlotCanBeReused) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 1> a{std::byte{'A'}};
    std::array<std::byte, 1> b{std::byte{'B'}};
    std::array<std::byte, 1> c{std::byte{'C'}};
    std::array<std::byte, 1> d{std::byte{'D'}};

    auto rid_a = heap_file.InsertRecord(a);
    auto rid_b = heap_file.InsertRecord(b);
    auto rid_c = heap_file.InsertRecord(c);

    ASSERT_TRUE(rid_a.has_value());
    ASSERT_TRUE(rid_b.has_value());
    ASSERT_TRUE(rid_c.has_value());

    ASSERT_TRUE(
        heap_file.DeleteRecord(*rid_b)
    );

    auto rid_d = heap_file.InsertRecord(d);

    ASSERT_TRUE(rid_d.has_value());

    EXPECT_EQ(rid_d->page_id, rid_b->page_id);
    EXPECT_EQ(rid_d->slot_id, rid_b->slot_id);

    auto stored = heap_file.GetRecord(*rid_d);

    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), d.size());

    EXPECT_EQ((*stored)[0], std::byte{'D'});
}

TEST_F(HeapFileTest, RecordsRemainIndependentAcrossPages) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 2500> page0_data{};
    std::array<std::byte, 2500> page1_data{};
    std::array<std::byte, 2500> updated_page0{};

    page0_data.fill(std::byte{'A'});
    page1_data.fill(std::byte{'B'});
    updated_page0.fill(std::byte{'C'});

    auto rid0 = heap_file.InsertRecord(page0_data);
    auto rid1 = heap_file.InsertRecord(page1_data);

    ASSERT_TRUE(rid0.has_value());
    ASSERT_TRUE(rid1.has_value());

    ASSERT_NE(rid0->page_id, rid1->page_id);

    ASSERT_TRUE(
        heap_file.UpdateRecord(
            *rid0,
            updated_page0
        )
    );

    auto stored0 = heap_file.GetRecord(*rid0);
    auto stored1 = heap_file.GetRecord(*rid1);

    ASSERT_TRUE(stored0.has_value());
    ASSERT_TRUE(stored1.has_value());

    EXPECT_TRUE(
        std::equal(
            stored0->begin(),
            stored0->end(),
            updated_page0.begin()
        )
    );

    EXPECT_TRUE(
        std::equal(
            stored1->begin(),
            stored1->end(),
            page1_data.begin()
        )
    );

    ASSERT_TRUE(
        heap_file.DeleteRecord(*rid0)
    );

    auto page1_after_delete =
        heap_file.GetRecord(*rid1);

    ASSERT_TRUE(page1_after_delete.has_value());

    EXPECT_TRUE(
        std::equal(
            page1_after_delete->begin(),
            page1_after_delete->end(),
            page1_data.begin()
        )
    );
}

TEST_F(HeapFileTest, ReopenHeapFilePreservesRecords) {
    RID saved_rid{};
    int directory_page_id = -1;

    std::array<std::byte, 4> data{
        std::byte{'A'},
        std::byte{'B'},
        std::byte{'C'},
        std::byte{'D'}
    };

    {
        HeapFile heap_file(*bpm);

        auto rid = heap_file.InsertRecord(data);

        ASSERT_TRUE(rid.has_value());

        saved_rid = *rid;

        directory_page_id = heap_file.GetFirstDirectoryPageId();

        ASSERT_NE(directory_page_id, -1);

        ASSERT_TRUE(bpm->FlushAllPages());
    }

    // Destroy the original BPM so this really tests
    // persistence through the file-backed DiskManager.
    bpm.reset();

    bpm = std::make_unique<BufferPoolManager>(
        2,
        db_path
    );

    HeapFile reopened(
        *bpm,
        directory_page_id
    );

    auto stored = reopened.GetRecord(saved_rid);

    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), data.size());

    EXPECT_TRUE(
        std::equal(
            stored->begin(),
            stored->end(),
            data.begin()
        )
    );
}

TEST_F(HeapFileTest, InsertCreatesDirectoryEntryWithCorrectFreeSpace) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 100> data{};
    data.fill(std::byte{'A'});

    auto rid = heap_file.InsertRecord(data);
    ASSERT_TRUE(rid.has_value());

    auto header = ReadDirectoryHeader(heap_file);
    ASSERT_EQ(header.num_entries, 1);

    auto entry = ReadDirectoryEntry(heap_file, 0);
    EXPECT_EQ(entry.page_id, rid->page_id);

    auto actual_free_space = ReadHeapPageFreeSpace(rid->page_id);
    EXPECT_EQ(entry.free_space, actual_free_space);
}

TEST_F(HeapFileTest, MultipleInsertsSamePageUpdateDirectoryFreeSpace) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 100> first{};
    std::array<std::byte, 100> second{};
    first.fill(std::byte{'A'});
    second.fill(std::byte{'B'});

    auto rid1 = heap_file.InsertRecord(first);
    ASSERT_TRUE(rid1.has_value());

    auto before = ReadDirectoryEntry(heap_file, 0);

    auto rid2 = heap_file.InsertRecord(second);
    ASSERT_TRUE(rid2.has_value());
    ASSERT_EQ(rid1->page_id, rid2->page_id);

    auto header = ReadDirectoryHeader(heap_file);
    EXPECT_EQ(header.num_entries, 1);

    auto after = ReadDirectoryEntry(heap_file, 0);
    EXPECT_LT(after.free_space, before.free_space);
    EXPECT_EQ(after.free_space, ReadHeapPageFreeSpace(rid1->page_id));
}

TEST_F(HeapFileTest, SpillToSecondPageAddsSecondDirectoryEntry) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 2500> first{};
    std::array<std::byte, 2500> second{};
    first.fill(std::byte{'A'});
    second.fill(std::byte{'B'});

    auto rid1 = heap_file.InsertRecord(first);
    auto rid2 = heap_file.InsertRecord(second);

    ASSERT_TRUE(rid1.has_value());
    ASSERT_TRUE(rid2.has_value());
    ASSERT_NE(rid1->page_id, rid2->page_id);

    auto header = ReadDirectoryHeader(heap_file);
    ASSERT_EQ(header.num_entries, 2);

    auto entry0 = ReadDirectoryEntry(heap_file, 0);
    auto entry1 = ReadDirectoryEntry(heap_file, 1);

    EXPECT_EQ(entry0.page_id, rid1->page_id);
    EXPECT_EQ(entry1.page_id, rid2->page_id);
    EXPECT_EQ(entry0.free_space, ReadHeapPageFreeSpace(rid1->page_id));
    EXPECT_EQ(entry1.free_space, ReadHeapPageFreeSpace(rid2->page_id));
}

TEST_F(HeapFileTest, DeleteRecordUpdatesDirectoryFreeSpace) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 500> data{};
    data.fill(std::byte{'A'});

    auto rid = heap_file.InsertRecord(data);
    ASSERT_TRUE(rid.has_value());

    auto before = ReadDirectoryEntry(heap_file, 0);

    ASSERT_TRUE(heap_file.DeleteRecord(*rid));

    auto after = ReadDirectoryEntry(heap_file, 0);
    EXPECT_GT(after.free_space, before.free_space);
    EXPECT_EQ(after.free_space, ReadHeapPageFreeSpace(rid->page_id));
}

TEST_F(HeapFileTest, UpdateRecordLargerDecreasesDirectoryFreeSpace) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 100> original{};
    std::array<std::byte, 500> updated{};
    original.fill(std::byte{'A'});
    updated.fill(std::byte{'B'});

    auto rid = heap_file.InsertRecord(original);
    ASSERT_TRUE(rid.has_value());

    auto before = ReadDirectoryEntry(heap_file, 0);

    ASSERT_TRUE(heap_file.UpdateRecord(*rid, updated));

    auto after = ReadDirectoryEntry(heap_file, 0);
    EXPECT_LT(after.free_space, before.free_space);
    EXPECT_EQ(after.free_space, ReadHeapPageFreeSpace(rid->page_id));
}

TEST_F(HeapFileTest, UpdateRecordSmallerIncreasesDirectoryFreeSpace) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 500> original{};
    std::array<std::byte, 100> updated{};
    original.fill(std::byte{'A'});
    updated.fill(std::byte{'B'});

    auto rid = heap_file.InsertRecord(original);
    ASSERT_TRUE(rid.has_value());

    auto before = ReadDirectoryEntry(heap_file, 0);

    ASSERT_TRUE(heap_file.UpdateRecord(*rid, updated));

    auto after = ReadDirectoryEntry(heap_file, 0);
    EXPECT_GT(after.free_space, before.free_space);
    EXPECT_EQ(after.free_space, ReadHeapPageFreeSpace(rid->page_id));
}

TEST_F(HeapFileTest, FailedUpdateDoesNotChangeDirectoryFreeSpace) {
    HeapFile heap_file(*bpm);

    std::array<std::byte, 500> original{};
    std::array<std::byte, 3000> filler{};
    std::array<std::byte, 2000> too_large{};
    original.fill(std::byte{'A'});
    filler.fill(std::byte{'B'});
    too_large.fill(std::byte{'C'});

    auto rid = heap_file.InsertRecord(original);
    auto filler_rid = heap_file.InsertRecord(filler);

    ASSERT_TRUE(rid.has_value());
    ASSERT_TRUE(filler_rid.has_value());
    ASSERT_EQ(rid->page_id, filler_rid->page_id);

    auto before = ReadDirectoryEntry(heap_file, 0);

    EXPECT_FALSE(heap_file.UpdateRecord(*rid, too_large));

    auto after = ReadDirectoryEntry(heap_file, 0);
    EXPECT_EQ(after.free_space, before.free_space);
    EXPECT_EQ(after.free_space, ReadHeapPageFreeSpace(rid->page_id));
}

