HeapFile manages a persistent collection of slotted data pages, using page-directory metadata to track ownership and free space, and exposes record-level CRUD through stable RIDs.

```
PageDirectory
    knows metadata about one directory page

HeapPage
    knows record layout within one data page

HeapFile
    knows how those pages form one logical heap file
```

### Initialization

There are two different initializations for HeapFile. `HeapFile(BufferPoolManager& bpm)` creates a new HeapFile and `HeapFile(BufferPoolManager& bpm, int first_directory_page_id)` reopens an already-existing HeapFile whose persistent metadata begins at this page.

Destroying the C++ HeapFile object does not destroy the logical structure, it is persisted.

### Invariants

Every owned data page has exactly one directory entry.

Directory cached free space matches HeapPage free space after successful modifications.

For the lifetime of a record `RID = (page_id, slot_id)` remains its logical location. HeapPage compaction may move physical bytes, but does not change the RID.

HeapFile directory root identifies the logical HeapFile. first_directory_page_id_ must refer to the first directory page belonging to this HeapFile.

HeapFile does not own the BufferPoolManager. `BufferPoolManager& bpm_;` is a non-owning reference so the BufferPoolManager must outlive the HeapFile.

### Concurrency

HeapFile itself has no mutex. Instead, it relies on the BufferPoolManager mutex and individual Page latches for lower-level synchronization.