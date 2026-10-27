# pragma once

#include "storage/page.h"

#include <cstdint>
#include <optional>
#include <span>

using Record = std::span<const std::byte>;   

struct HeapPageHeader {
    std::uint16_t slot_count;
    std::uint16_t free_start; // next free slot offset (grows upwards)
    std::uint16_t free_end; // first occupied record offset (grows downwards, i.e. decrements on insert)
};

struct SlotEntry {
    std::uint16_t offset;
    std::uint16_t length;

    bool IsValid() const {
        return offset != 0 && length != 0;
    }
};

static_assert(sizeof(HeapPageHeader) == 6, "Unexpected HeapPageHeader layout");
static_assert(sizeof(SlotEntry) == 4, "Unexpected SlotEntry layout");

constexpr std::size_t MAX_RECORD_SIZE = PAGE_SIZE - sizeof(HeapPageHeader) - sizeof(SlotEntry);

class HeapPage {
private:
    Page& page_;

    HeapPageHeader ReadHeader() const;

    void WriteHeader(const HeapPageHeader& header);

    SlotEntry ReadSlot(std::uint16_t slot_id) const;

    void WriteSlot(std::uint16_t slot_id, const SlotEntry& slot);

    void Compact();

    bool RelocateRecord(std::uint16_t slot_id, SlotEntry& slot, const Record& rec);

public:
    explicit HeapPage(Page& page): page_(page) {}

    void Initialize();

    std::optional<std::uint16_t> InsertRecord(const Record& rec);
    bool DeleteRecord(std::uint16_t slot_id);
    std::optional<Record> GetRecord(std::uint16_t slot_id) const;
    bool UpdateRecord(std::uint16_t slot_id, const Record& rec);

    std::uint16_t GetFreeSpace() const;
    std::uint16_t GetSlotCount() const;
};