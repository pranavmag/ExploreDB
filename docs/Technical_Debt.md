## Disk Manager Technical Debt

### IsValidPageId() has a concurrency inconsistency

All the normal operations of disk manager lock using a mutex but `bool DiskManager::IsValidPageId(int page_id) const` does not. This function can be called while another thread is executing `AllocatePage()`. `AllocatePage()` changes the file size and `IsValidPageId()` checks on that file size to ensure a valid page id.

### There is no actual crash-durability guarantee yet

`file_.flush` flushes C++ stream buffers but does not guarantee that the bytes survived a power loss. Eventually true durability involves operating-system mechanisms such as fsync/fdatasync on POSIX systems and interacts heavily with WAL.

### Page allocation is append-only

V1 page allocation is monotonic and append-only; physical page reclamation is not implemented.

### There is no database file header / magic number

Currently, any file whose size happens to be divisible by 4096 is considered a valid database: random_file.bin of size = 8192 → accepted.

### Parent directories aren't created

If someone gives `/data/mydb/database.db` and `/data/mydb/` doesn't exist, creation fails. The DiskManager isn't obligated to create arbitrary directory tree but I still want to note it down.

### int gives a maximum page-ID range

At 4096 bytes/page, INT_MAX pages corresponds to roughly 8 TiB of page-addressable storage. At the current stage of the project that is fine but I am noting it down for future reference if `std::uint64_t` might be needed to be used later.

## Heap Page/Page Directory Technical Debt

### Raw struct layout is part of the disk format for HeapPage/PageDirectory

HeapPageHeader and SlotEntry are persisted with memcpy which means that the format currently depends on structure layout, field sizes, padding, and endianess. The static assertions protect the sizes, but not endianness or every ABI issue. Eventually, if portability/versioned storage matters, I'd have to explicitly encode integer fields.

### No validation of corrupted HeapPage/PageDirectory metadata

`GetFreeSpace()` just returns the space, automatically assuming that there is space and `ReadSlot()` assumes the requested slot refers to valid page metadata. They don't take disk corruption into account because right now it is assumed that any page interpreted as a HeapPage was created and maintained by this engine.

### No page type / magic marker

There is no way to distinguish between what is considered a directory page or heap page which is okay for now because higher layers control usage but I still want to note it down.

### Compaction allocates heap memory

For compaction it uses `std::vector<LiveSlot> live_slots;` which means a page-level operation performs a dynamic allocation. A more performance-sensitive engine might use different algorithms or reusable scratch memory.

### 16-bit offsets tie the format to relatively small pages

`std::uint16_t` is used for offset, length, free_start, and free_end which is good for 4096-byte pages but it means that the physical layout cannot represent offsets beyond 65535.

### Directory search is currently linear

Finding a particular data page for UpdateDirectoryFreeSpace() requires scanning directory page, scanning entries, and possibly following next directory. So lookup is currently linear in the number of HeapFile pages. At the current scale it is fine but later it can turn into experimenting with linear directory, free-space buckets, page-id map, and free-space map/tree.

### Buffer Pool Manager Technical Debt

Buffer frame reassignment is not strongly exception-safe. An I/O or allocation exception during a multi-step frame transition may leave partially updated internal state. Same thing in NewPage() after calling AcquireFrame() there is a failure window before the new page is installed into the frame.

Raw Page* instead of RAII page guards is not ideal. Page* FetchPage() and Page* NewPage() combined with manual UnpinPage() means that correctness depends on the callers pairing operations correctly.

All BPM metadata operations are serialized behind the bpm mutex which means that two unrelated cache hits both pass through the same mutex which limits scalability.