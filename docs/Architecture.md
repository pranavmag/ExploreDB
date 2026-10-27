# Architecture

## Overview

This project is an educational and experimental relational database engine written in C++.

The goal is to build a functioning relational DBMS while exposing important database algorithms and implementation choices for experimentation, benchmarking, and comparison.

The engine is intentionally page-oriented and currently follows a traditional relational storage architecture inspired by systems such as PostgreSQL.

The project is not intended to compete with production database systems. Its purpose is to provide a concrete environment for studying database internals, implementing alternative algorithms, and measuring their behavior under controlled workloads.

A major project goal is to make important internal algorithms observable and, where appropriate, interchangeable.

Examples include:

- buffer replacement policies
- external sorting strategies
- join algorithms
- indexing structures
- storage-management policies

C++ implements the database engine and algorithms.

Python will be used extensively for benchmark orchestration, data analysis, and visualization.

---

## High-Level Architecture

The current storage architecture is:

```text
HeapFile
    │
    ├── persistent ownership of data pages
    ├── record-level CRUD
    ├── page selection for insertion
    └── directory metadata maintenance
    │
    ├───────────────┐
    ▼               ▼
PageDirectory     HeapPage
    │               │
    │               │
    └───────┬───────┘
            ▼
           Page
            │
            ▼
    BufferPoolManager
            │
      ┌─────┴─────┐
      ▼           ▼
 PageTable    Replacement Policy
                  │
                  ▼
                Frame
            │
            ▼
       DiskManager
            │
            ▼
      Database File
```

The architecture is divided into layers with intentionally narrow responsibilities.

Higher layers should rely on lower-level abstractions without needing to know their implementation details.

---

## Storage Layer

### DiskManager

`DiskManager` is the lowest persistent-storage abstraction.

It treats the database file as a sequence of fixed-size pages.

```text
page 0 -> bytes [0, PAGE_SIZE)
page 1 -> bytes [PAGE_SIZE, 2 * PAGE_SIZE)
page 2 -> bytes [2 * PAGE_SIZE, 3 * PAGE_SIZE)
```

Its responsibilities include:

- creating and opening the database file
- allocating fixed-size physical pages
- mapping page IDs to file offsets
- reading complete pages
- writing complete pages
- serializing file I/O

It does not understand records, heap files, indexes, slots, or queries.

Detailed design:

`docs/storage/Disk_Manager.md`

---

### Page

`Page` represents one logical database page in memory.

It owns:

- a fixed-size byte array
- a page ID
- a read/write latch

`Page` does not interpret the meaning of its bytes.

Higher-level components such as `HeapPage` and `PageDirectory` provide typed interpretations of those bytes.

---

### HeapPage

`HeapPage` interprets a generic `Page` as a slotted page containing variable-length records.

The page layout grows inward from both ends:

```text
low addresses                                  high addresses

[Header][Slot 0][Slot 1][... free ...][Records]
                     ->              <-
                 free_start       free_end
```

The slot directory provides stable logical record identifiers even when records move during page compaction.

A record inside a HeapFile is identified by:

```cpp
RID {
    page_id,
    slot_id
}
```

Important properties include:

- variable-length record storage
- stable slot IDs
- deleted-slot reuse
- eager compaction
- record updates
- contiguous free-space tracking

Detailed design:

`docs/storage/Heap_Page.md`

---

### PageDirectory

`PageDirectory` interprets a generic `Page` as persistent metadata describing HeapFile data pages.

Each directory entry currently stores:

```text
page_id
cached free_space
```

The directory allows HeapFile insertion to avoid loading every data page when searching for available space.

The cached free-space value is a search optimization.

`HeapPage::InsertRecord()` remains the final authority on whether a record actually fits.

Directory pages form a linked chain using:

```text
next_dir_page
```

This allows a HeapFile to grow beyond the capacity of a single directory page.

Detailed design:

`docs/storage/Page_Directory.md`

---

### HeapFile

`HeapFile` manages a persistent collection of HeapPages.

Its primary responsibilities are:

- creating and reopening heap files
- maintaining persistent ownership of data pages
- inserting records
- retrieving records by RID
- deleting records
- updating records
- maintaining PageDirectory free-space metadata
- traversing and extending the directory-page chain

A HeapFile is identified by:

```text
first_directory_page_id
```

The first directory page acts as the persistent root of the HeapFile.

A newly created HeapFile allocates and initializes this page.

An existing HeapFile can later be reopened using the same directory-page ID.

Mutating record operations maintain the invariant:

```text
PageDirectoryEntry.free_space
    ==
HeapPage::GetFreeSpace()
```

after successful insert, delete, or update operations.

Detailed design:

`docs/storage/Heap_File.md`

---

## Buffer Management Layer

The buffer-management layer sits between logical page users and physical disk I/O.

Its job is to keep frequently accessed pages in memory while controlling when pages are loaded, pinned, evicted, and written back.

### BufferPoolManager

`BufferPoolManager` coordinates the lifecycle of cached pages.

Its responsibilities include:

- mapping page IDs to frames
- loading pages from disk
- allocating new pages
- pinning and unpinning pages
- tracking dirty pages
- flushing modified pages
- selecting frames for eviction through a replacement policy

Higher-level storage components request pages through the BufferPoolManager rather than interacting with DiskManager directly.

Detailed design:

`docs/buffer/Buffer_Pool_Manager.md`

---

### Frame

A frame is one slot in the in-memory buffer pool.

A frame contains:

- a `Page`
- a frame ID
- a pin count
- dirty-state metadata

The frame represents the in-memory location in which a database page currently resides.

Detailed design:

`docs/buffer/Frame.md`

---

### PageTable

The page table maintains the mapping:

```text
page_id -> frame_id
```

This allows BufferPoolManager to determine whether a requested page is already resident in memory.

Detailed design:

`docs/buffer/Page_Table.md`

---

### Replacement Policy

When the buffer pool contains no empty frame, a replacement policy chooses an eligible frame for eviction.

The current implementation uses Clock replacement.

Replacement policies are expected to become an important experimental boundary in the project.

Planned policies include:

- Clock
- LRU
- LRU-K
- potentially ARC and other policies

The long-term goal is to compare replacement strategies under identical workloads rather than simply selecting one implementation permanently.

Metrics may include:

- cache hits
- cache misses
- eviction count
- disk reads
- dirty-page writes
- latency
- throughput
- policy CPU overhead
- policy memory overhead

Detailed replacement-policy documentation belongs under:

`docs/buffer/`

---

## Page Access and Locking Model

The current system separates page residency from page-content synchronization.

BufferPoolManager is responsible for:

```text
page residency
pin counts
frame assignment
dirty state
replacement
```

Individual `Page` objects provide read/write latches for protecting page contents.

Higher-level components such as HeapFile acquire page latches before interpreting or modifying page bytes.

General lock ordering is:

```text
BufferPoolManager coordination
    ->
Page latch
```

Code should avoid holding a page latch while performing operations that may need to reacquire BufferPoolManager state.

Similarly, higher-level code avoids keeping unnecessary pages pinned while fetching other pages so that small buffer pools remain functional.

---

## Persistence Model

The database currently uses write-back buffering.

A page mutation generally follows:

```text
modify page in memory
    ->
mark page dirty
    ->
write later during eviction or explicit flush
```

A dirty page is not necessarily immediately persisted to disk.

Explicit flushing is required when tests or higher-level code need to establish a persistence boundary.

The engine currently does not provide full crash durability or transactional recovery.

Those concerns are tracked separately in:

`docs/Technical_Debt.md`

---

## Record Ownership and Lifetime

`HeapPage::GetRecord()` returns a non-owning span into the underlying Page memory.

That view is only valid while the underlying page remains resident and unchanged.

`HeapFile::GetRecord()` therefore copies the record into an owning buffer before unpinning the page.

This creates the boundary:

```text
HeapPage
    -> non-owning record view

HeapFile
    -> owning record result
```

---

## Current Data Flow

A typical record insertion follows:

```text
HeapFile::InsertRecord()
    ↓
read PageDirectory metadata
    ↓
select candidate HeapPage
    ↓
BufferPoolManager::FetchPage()
    ↓
HeapPage::InsertRecord()
    ↓
mark data page dirty
    ↓
update PageDirectory cached free space
    ↓
mark directory page dirty
```

If no existing HeapPage can hold the record:

```text
allocate new Page
    ↓
initialize as HeapPage
    ↓
insert record
    ↓
append PageDirectoryEntry
```

If the current PageDirectory is full:

```text
allocate new PageDirectory
    ↓
initialize it
    ↓
link previous directory through next_dir_page
    ↓
continue insertion using the new directory
```

---

## Design Philosophy

The project follows several architectural principles.

### Prefer concrete implementations before premature abstractions

New interfaces should be introduced when multiple real implementations reveal a useful common boundary.

The project should not attempt to predict every future storage or execution architecture in advance.

---

### Keep component responsibilities narrow

Examples:

```text
DiskManager
    -> physical page I/O

BufferPoolManager
    -> cached page lifecycle

HeapPage
    -> record layout inside one page

PageDirectory
    -> persistent HeapFile page metadata

HeapFile
    -> logical collection of record pages
```

---

### Make internal behavior observable

The engine is intended to support experimentation.

Important internal behavior should eventually be measurable through counters, traces, and benchmark output.

Examples include:

- page accesses
- buffer hits and misses
- evictions
- disk reads and writes
- dirty-page flushes
- algorithm execution time
- memory overhead

---

### Treat benchmarking as part of the architecture

C++ is responsible for implementing and executing database algorithms.

Python is responsible for:

- generating or orchestrating benchmark workloads
- loading benchmark output
- statistical analysis
- visualization
- comparing alternative implementations

The expected workflow is:

```text
C++ engine
    ↓
benchmark runner
    ↓
structured metrics
    ↓
Python
    ↓
pandas / NumPy / Matplotlib
    ↓
analysis and visualization
```

---

## Planned Architecture

The current implementation is concentrated primarily in storage and buffer management.

The long-term relational architecture is expected to evolve toward:

```text
SQL
 ↓
Parser
 ↓
Binder / Semantic Analysis
 ↓
Logical Plan
 ↓
Optimizer
 ↓
Physical Execution Operators
 ↓
Heap Files / Indexes
 ↓
Buffer Pool
 ↓
Disk Manager
 ↓
Database File
```

Future major areas include:

- B+ tree indexing
- external sorting
- relational operators
- SQL parsing
- query planning
- join algorithms
- query optimization
- transactions and concurrency control
- write-ahead logging and recovery where appropriate
- benchmark infrastructure
- alternative implementations of important algorithms

These are long-term directions rather than commitments to a final production architecture.

---

## Known Limitations

Important known limitations and unresolved architectural questions are maintained separately in:

`docs/Technical_Debt.md`

This file should describe the intended architecture and current component boundaries rather than become a running list of unfinished work.