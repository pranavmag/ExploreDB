PageDirectory is a typed view over a generic Page that stores metadata describing the HeapFile’s data pages, currently their page IDs and cached free space.

Unlike HeapPage, nothing grows from the other end so the page layout looks like:

low addresses                                      high addresses
┌─────────────────┬─────────┬─────────┬─────────┬──────────────┐
│ DirectoryHeader │ Entry 0 │ Entry 1 │ Entry 2 │ unused space │
└─────────────────┴─────────┴─────────┴─────────┴──────────────┘

At 4096-byte pages: (4096 - 8) / 8 = 511 entries, so one directory page can describe 511 heap data pages.

### Initialization

A DirectoryPageHeader is initialized with the next directory page as -1 and the number of entries as 0. When the next directory page is -1 that represents the final directory page and when the number of entries is 0 that represents no data pages in the directory.

Like HeapPage::Initialize(), the bytes are not zeroed. Even if stale bytes exist after the header, none of those bytes are logically valid directory entries because `num_entries` is 0.

### Invariants

For every valid directory entry, entry.free_space should equal the free space reported by the corresponding HeapPage after the most recent successful mutation.

One PageDirectory wraps exactly one Page, it owns no memory. It simply interprets a supplied page as directory metadata.

Entry count must fit within physical capacity. `0 <= num_entries <= DIRECTORY_ENTRY_CAPACITY` and with the current format being `DIRECTORY_ENTRY_CAPACITY = 511`.

Valid entries are contiguous. Unlike HeapPage slots, directory entries don't currently have deletion holes.

Once a data page gets a directory entry, it stays there. Even if every record on that page gets deleted, the entry remains. This is useful because the page can be reused and only if I eventually support physical page deallocation would I need a way to remove/reclaim directory entries.

### Concurrency

Same philosophy as HeapPage. PageDirectory does no locking itself.

