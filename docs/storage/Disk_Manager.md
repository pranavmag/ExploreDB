The disk manager is a single-file, fixed-size page store.

A database file is a sequence of PAGE_SIZE byte regions addressed by integer page IDs.

```
page 0: bytes [0, 4095]
page 1: bytes [4096, 8191]
page 2: bytes [8192, 12287]
```

The DiskManager is responsible for:

```
- opening and creating the database file
- allocating fixed-size pages
- translating page IDs to byte offsets
- reading complete pages
- writing complete pages
- serializing file I/O through its mutex
```

The disk_manager does not understand components like

```
HeapPage
PageDirectory
records
slots
indexes
RIDs
queries
```

### Construction

`DiskManager::DiskManager(const fs::path& file_path)` constructs the disk manager and remembers the path. If the file doesn't exist then it creates an empty database file and reopens it as `std::fstream` with `std::ios::in | std::ios::out | std::ios::binary` so one stream can both read and write pages.

### Invariants

Every database file must contain an integral number of pages. For example, a 10,000 byte database file is invalid because it would indicate a partial page. File alignment is validated when the database is opened and before extending the file during page allocation.

A valid page ID satisfies `0 <= page_id < page_count` where `page_count = file_size / PAGE_SIZE`.

A page ID directly identifies its physical location through `offset = page_id * PAGE_SIZE`.

There is a fixed size I/O meaning that every successful read/write transfers exactly `PAGE_SIZE` bytes. No partial logical page operations exist.

Allocated pages are appended and initially zero-filled.

There is no deallocation of pages. Once page N exists, it continues to exist. This is a current limitation of the engine.

The WritePage() method accepts `WritePage(int page_id, const Page& page)` so theoretically someone could have `page.GetPageId() = 7` and then do `WritePage(9, page);` and the disk manager wouldn't complain. Currently that is okay because the buffer pool manager handles these calls correctly.

ReadPage() does not assign the Page ID, it copies bytes but doesn't do `page.Reset(page_id)`. The buffer pool manager currently does assignment before reading. BufferPoolManager owns logical Page/frame identity while DiskManager only transfers PAGE_SIZE bytes to or from the supplied Page buffer.

### Concurrency

Disk I/O operations are serialized using `disk_mutex_`.

`AllocatePage()`, `ReadPage()`, and `WritePage()` acquire this mutex before interacting with the file stream.

`IsValidPageId()` currently does not acquire the mutex; this is tracked in `Technical_Debt.md`.

### Current Limitations

- single database file
- fixed PAGE_SIZE
- append-only page allocation
- no page reclamation
- no crash recovery guarantee