# pragma once

#include "buffer/buffer_pool_manager.h"
#include "storage/heap_page.h"

#include <cstdint>
#include <optional>
#include <vector>

using OwnedRecord = std::vector<std::byte>;

struct RID {
    int page_id;
    std::uint16_t slot_id;
};

class HeapFile {
private:
    BufferPoolManager& bpm_;
    int first_directory_page_id_;

    void UpdateDirectoryFreeSpace(int data_page_id, std::uint16_t free_space);

public:
    explicit HeapFile(BufferPoolManager& bpm);

    HeapFile(
        BufferPoolManager& bpm,
        int first_directory_page_id
    );

    [[nodiscard]] int GetFirstDirectoryPageId() const;

    std::optional<RID> InsertRecord(const Record& record);

    std::optional<OwnedRecord> GetRecord(const RID& rid);

    bool DeleteRecord(const RID& rid);

    bool UpdateRecord(const RID& rid, const Record& rec);
};