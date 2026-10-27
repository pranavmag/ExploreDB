#pragma once

#include "storage/page.h"

#include <cstddef>
#include <cstdint>
#include <limits>

struct PageDirectoryEntry {
    int page_id;
    std::uint16_t free_space;
};

struct DirectoryPageHeader {
    int next_dir_page;
    std::uint16_t num_entries;
};

static_assert(sizeof(PageDirectoryEntry) == 8, "Unexpected PageDirectoryEntry layout");

static_assert(sizeof(DirectoryPageHeader) == 8, "Unexpected DirectoryPageHeader layout");


constexpr std::size_t DIRECTORY_ENTRY_CAPACITY = 
(PAGE_SIZE - sizeof(DirectoryPageHeader)) / sizeof(PageDirectoryEntry);

static_assert(DIRECTORY_ENTRY_CAPACITY <= std::numeric_limits<std::uint16_t>::max());

class PageDirectory {
private:
    Page& page_;

public:
    explicit PageDirectory(Page& page): page_(page) {}

    void Initialize();

    DirectoryPageHeader ReadHeader() const;

    void WriteHeader(const DirectoryPageHeader& header);

    PageDirectoryEntry ReadEntry(std::uint16_t index) const;

    void WriteEntry(std::uint16_t index, const PageDirectoryEntry& entry);

    std::uint16_t GetEntryCount() const;
};