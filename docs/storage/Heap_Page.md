HeapPage is a typed view over a generic Page that interprets its bytes as a slotted page containing variable-length records.

HeapPage does not own the Page, it interprets and modifies the bytes inside one.

The page layout is like this:

low addresses                                      high addresses
┌──────────────┬────────┬────────┬───────┬─────────┬─────────┐
│ HeapPageHdr  │ Slot 0 │ Slot 1 │ FREE  │ Record B│ Record A│
└──────────────┴────────┴────────┴───────┴─────────┴─────────┘
               → free_start                 free_end ←

```
The header tracks

slot_count - number of allocated slot-directory entries

free_start - first byte after the slot directory

free_end - first byte of packed record storage
```

### Initialization

The HeapPageHeader is constructed with the slot count as 0, the free start being at the size of the header, and the free end is at the PAGE_SIZE.

Initialize() does not need to zero the entire page. There may be stale bytes elsewhere, but because `slot_count = 0` none of them represent valid records.

### Invariants

HeapPage::GetRecord() returns a non-owning view into the underlying page. The view must not outlive the page's residency/pin lifetime and may be invalidated by record movement, compaction, updates, or frame reuse.

For slot i: `offset = sizeof(HeapPageHeader) + i * sizeof(SlotEntry);`

All live records occupy bytes between `free_end ... PAGE_SIZE` and usable contiguous free space is `[free_start, free_end)`.

Compaction may change a record's byte offset but must not change its slot_id.

A slot is currently invalid when `offset == 0 || length == 0` and deletion sets both to zero. Deleted slots remain part of the slot directory and may later be reused.

HeapPage owns no memory, it is only `Page& page_;`. It is a view/controller over an externally owned Page.

Right now compaction uses eager compaction which can be expensive. It will be useful to test against lazy fragmentation and threshold-based compaction in the future.

### Concurrency

HeapPage itself does no locking. None of the operations acquire a `Page::WriteLatch()` because the caller does it.