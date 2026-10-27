#include "storage/heap_file.h"
#include "storage/page_directory.h"

HeapFile::HeapFile(BufferPoolManager& bpm)
    : bpm_(bpm), first_directory_page_id_(-1) {

    Page* page =
        bpm_.NewPage(first_directory_page_id_);

    if (page == nullptr) {
        throw std::runtime_error(
            "Failed to create heap file directory"
        );
    }

    {
        auto guard = page->WriteLatch();

        PageDirectory directory(*page);

        directory.Initialize();
    }

    bpm_.UnpinPage(
        first_directory_page_id_,
        true
    );
}

HeapFile::HeapFile(BufferPoolManager& bpm, int first_directory_page_id)
    : bpm_(bpm),
      first_directory_page_id_(first_directory_page_id) {}

int HeapFile::GetFirstDirectoryPageId() const {
    return first_directory_page_id_;
}

void HeapFile::UpdateDirectoryFreeSpace(int data_page_id, std::uint16_t free_space) {
    int directory_page_id = first_directory_page_id_;

    while (directory_page_id != -1) {
        Page* dir_page = bpm_.FetchPage(directory_page_id);

        if (dir_page == nullptr) {
            throw std::runtime_error(
                "Failed to fetch HeapFile directory page"
            );
        }

        bool found = false;
        DirectoryPageHeader header{};

        {
            auto guard = dir_page->WriteLatch();

            PageDirectory directory(*dir_page);

            header = directory.ReadHeader();

            for (std::uint16_t i = 0;
                 i < header.num_entries;
                 ++i) {

                auto entry = directory.ReadEntry(i);

                if (entry.page_id == data_page_id) {
                    entry.free_space = free_space;
                    directory.WriteEntry(i, entry);

                    found = true;
                    break;
                }
            }
        }

        bpm_.UnpinPage(directory_page_id, found);

        if (found) {
            return;
        }

        directory_page_id = header.next_dir_page;
    }

    throw std::runtime_error(
        "HeapFile directory missing entry for data page"
    );
}

std::optional<RID> HeapFile::InsertRecord(const Record& record) {
    if (record.empty()) {
        return std::nullopt;
    }

    if (record.size() > MAX_RECORD_SIZE) {
        return std::nullopt;
    }

    int directory_page_id = first_directory_page_id_;

    while (directory_page_id != -1) {
        Page* dir_page = bpm_.FetchPage(directory_page_id);

        if (dir_page == nullptr) {
            return std::nullopt;
        }

        DirectoryPageHeader header{};
        std::vector<PageDirectoryEntry> entries;

        {
            auto guard = dir_page->ReadLatch();

            PageDirectory directory(*dir_page);

            header = directory.ReadHeader();

            entries.reserve(header.num_entries);

            for (std::uint16_t i = 0; i < header.num_entries; ++i) {
                entries.push_back(directory.ReadEntry(i));
            }
        }

        bpm_.UnpinPage(directory_page_id, false);

        // Try existing data pages described by this directory.
        for (std::uint16_t i = 0; i < entries.size(); ++i) {
            PageDirectoryEntry entry = entries[i];

            // Cached free-space metadata is only a fast rejection.
            if (entry.free_space < record.size()) {
                continue;
            }

            Page* page = bpm_.FetchPage(entry.page_id);

            if (page == nullptr) {
                continue;
            }

            std::optional<std::uint16_t> slot_id;
            std::uint16_t new_free_space = 0;

            {
                auto guard = page->WriteLatch();

                HeapPage heap_page(*page);

                slot_id = heap_page.InsertRecord(record);

                if (slot_id.has_value()) {
                    new_free_space = heap_page.GetFreeSpace();
                }
            }

            if (!slot_id.has_value()) {
                bpm_.UnpinPage(entry.page_id, false);
                continue;
            }

            // The data page has now been modified successfully.
            bpm_.UnpinPage(entry.page_id, true);

            // Metadata synchronization is now mandatory.
            dir_page = bpm_.FetchPage(directory_page_id);

            if (dir_page == nullptr) {
                throw std::runtime_error(
                    "Failed to fetch HeapFile directory page during metadata update"
                );
            }

            {
                auto guard = dir_page->WriteLatch();

                PageDirectory directory(*dir_page);

                entry.free_space = new_free_space;
                directory.WriteEntry(i, entry);
            }

            bpm_.UnpinPage(directory_page_id, true);

            return RID{
                entry.page_id,
                *slot_id
            };
        }

        // Continue through an existing directory chain.
        if (header.next_dir_page != -1) {
            directory_page_id = header.next_dir_page;
            continue;
        }

        // This is the final directory page.
        // If it is full, create and link another one.
        if (header.num_entries >= DIRECTORY_ENTRY_CAPACITY) {
            int new_directory_page_id = -1;

            Page* new_directory_page =
                bpm_.NewPage(new_directory_page_id);

            if (new_directory_page == nullptr) {
                return std::nullopt;
            }

            {
                auto guard = new_directory_page->WriteLatch();

                PageDirectory new_directory(*new_directory_page);
                new_directory.Initialize();
            }

            bpm_.UnpinPage(new_directory_page_id, true);

            // Link the current directory page to the new directory.
            dir_page = bpm_.FetchPage(directory_page_id);

            if (dir_page == nullptr) {
                throw std::runtime_error(
                    "Failed to fetch directory page while linking directory chain"
                );
            }

            {
                auto guard = dir_page->WriteLatch();

                PageDirectory directory(*dir_page);

                auto current_header = directory.ReadHeader();

                current_header.next_dir_page = new_directory_page_id;

                directory.WriteHeader(current_header);
            }

            bpm_.UnpinPage(directory_page_id, true);

            directory_page_id = new_directory_page_id;

            continue;
        }

        // No existing data page could fit the record,
        // and the current directory has room for another entry.
        int new_page_id = -1;

        Page* new_page =
            bpm_.NewPage(new_page_id);

        if (new_page == nullptr) {
            return std::nullopt;
        }

        std::optional<std::uint16_t> slot_id;
        std::uint16_t remaining_free_space = 0;

        {
            auto guard = new_page->WriteLatch();

            HeapPage heap_page(*new_page);

            heap_page.Initialize();

            slot_id = heap_page.InsertRecord(record);

            if (slot_id.has_value()) {
                remaining_free_space =
                    heap_page.GetFreeSpace();
            }
        }

        if (!slot_id.has_value()) {
            bpm_.UnpinPage(new_page_id, false);
            return std::nullopt;
        }

        // The new data page now contains the record.
        bpm_.UnpinPage(new_page_id, true);

        // Add persistent ownership + free-space metadata.
        dir_page = bpm_.FetchPage(directory_page_id);

        if (dir_page == nullptr) {
            throw std::runtime_error(
                "Failed to fetch HeapFile directory page while adding new data page"
            );
        }

        {
            auto guard = dir_page->WriteLatch();

            PageDirectory directory(*dir_page);

            auto current_header = directory.ReadHeader();

            PageDirectoryEntry new_entry{
                new_page_id,
                remaining_free_space
            };

            directory.WriteEntry(
                current_header.num_entries,
                new_entry
            );

            ++current_header.num_entries;

            directory.WriteHeader(current_header);
        }

        bpm_.UnpinPage(directory_page_id, true);

        return RID{
            new_page_id,
            *slot_id
        };
    }

    return std::nullopt;
}

std::optional<OwnedRecord> HeapFile::GetRecord(const RID &rid) {
    int page_id = rid.page_id;
    std::uint16_t slot_id = rid.slot_id;

    Page* page = bpm_.FetchPage(page_id);

    if (page == nullptr) {
        return std::nullopt;
    }

    std::optional<OwnedRecord> result;

    {
        auto guard = page->ReadLatch();

        HeapPage heap_page(*page);

        auto record = heap_page.GetRecord(slot_id);

        if (record.has_value()) {
            result = OwnedRecord(
                record->begin(),
                record->end()
            );
        }
    }

    bpm_.UnpinPage(page_id, false);

    return result;
}

bool HeapFile::DeleteRecord(const RID &rid) {
    int page_id = rid.page_id;
    std::uint16_t slot_id = rid.slot_id;
    
    Page *page = bpm_.FetchPage(page_id);

    if (page == nullptr) {
        return false;
    }

    bool is_deleted = false;
    std::uint16_t remaining_free_space = 0;

    {
        auto guard = page->WriteLatch();

        HeapPage heap_page(*page);

        is_deleted = heap_page.DeleteRecord(slot_id);

        if (is_deleted) {
            remaining_free_space = heap_page.GetFreeSpace();
        }
    }

    bpm_.UnpinPage(page_id, is_deleted);

    if (!is_deleted) {  
        return false;
    }

    UpdateDirectoryFreeSpace(page_id, remaining_free_space);

    return true;
}

bool HeapFile::UpdateRecord(const RID &rid, const Record &rec) {
    int page_id = rid.page_id;
    std::uint16_t slot_id = rid.slot_id;

    Page* page = bpm_.FetchPage(page_id);

    if (page == nullptr) {
        return false;
    }

    bool is_updated = false;
    std::uint16_t remaining_free_space = 0;

    {
        auto guard = page->WriteLatch();

        HeapPage heap_page(*page);

        is_updated = heap_page.UpdateRecord(slot_id, rec);

        if (is_updated) {
            remaining_free_space = heap_page.GetFreeSpace();
        }
    }

    bpm_.UnpinPage(page_id, is_updated);

    if (!is_updated) {
        return false;
    }

    UpdateDirectoryFreeSpace(page_id, remaining_free_space);

    return true;
}