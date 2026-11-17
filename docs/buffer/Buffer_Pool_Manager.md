BufferPoolManager manages the residency and lifecycle of database pages in a fixed-size in-memory buffer pool, coordinating page lookup, pinning, replacement, dirty-page write-back, and physical page I/O.

If the page is resident:

```
logical page_id
      ↓
PageTable
      ↓
frame_id
      ↓
Frame
      ↓
Page bytes
```

If the page is not resident:

```
page_id
   ↓
DiskManager
   ↓
AcquireFrame()
   ↓
Frame
   ↓
PageTable mapping
```

### Invariants

A resident page should exactly one page table mapping `page_id -> frame_id`. That frame's `Page::GetPageId()` should agree with the mapping.

A frame with pin_count > 0 should not be selected for eviction.

A dirty page must be written to disk before its frame is reassigned.

An empty frame is preferred over eviction.

After a succesful FetchPage() or NewPage() call the `pin_count += 1` and the reference bit should be set to 1. The caller is responsible for calling UnpinPage().

AcquireFrame() gives a frame into which a page can be put into. It searches empty frames and if it can't find one it asks CLOCK for a victim, flushes that page if dirty, removes the old page table mapping and returns that frame.

LoadPageIntoFrame() loads and existing physical page into the buffer pool. It validates the page_id, gets the frame, assigns page identity, reads the bytes, and installs the page table mapping.

NewPage() allocates a brand new disk page and places it in a frame

Pinning means the page is temporarily unavailable for eviction until the corresponding pins are released.

### Concurrency

The BPM mutex protects frame assignment, page table, pin counts, dirty flags, replacement state, and stats. 

The BPM expects `BPM mutex -> Page latch` so a caller should not hold a page latch while calling back into BPM operations such as UnpinPage(). This would invert the model to `Page latch -> BPM mutex` and create a deadlock.
