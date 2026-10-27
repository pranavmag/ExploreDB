# ExploreDB

**ExploreDB** is an educational and experimental relational database engine written in modern C++.

The project is built from scratch to explore how database systems work internally: page storage, buffer management, record layout, indexing, query execution, and eventually transactions and recovery.

Rather than aiming to compete with production systems such as PostgreSQL or DuckDB, ExploreDB is intended to become a platform for **implementing, comparing, and benchmarking database algorithms under controlled workloads**.

A major long-term goal is to make important internal policies interchangeable and observable. For example, the same workload could be executed using CLOCK, LRU, or LRU-K replacement and analyzed using Python to understand where each policy succeeds or fails.

---

## Project Goals

ExploreDB has three primary goals:

1. **Build database internals from first principles**
   - Implement the storage, buffer, indexing, and execution layers directly in C++.
   - Understand the invariants and tradeoffs behind each subsystem.

2. **Make database algorithms observable and experimental**
   - Support alternative implementations of important algorithms.
   - Expose metrics such as cache hits, misses, evictions, disk I/O, latency, and throughput.
   - Compare behavior across controlled workloads.

3. **Serve as a long-term systems programming project**
   - Practice large-scale C++ architecture, concurrency, testing, persistence, and performance analysis.
   - Favor clear component boundaries and documented design decisions over rapidly accumulating features.

---

## Current Architecture

The engine currently implements the storage and buffer-management foundations.

```text
HeapFile
    │
    ├── record-level CRUD
    ├── persistent page ownership
    ├── page selection
    └── free-space metadata maintenance
    │
    ├───────────────┐
    ▼               ▼
PageDirectory     HeapPage
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
 PageTable    ClockReplacer
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

### Storage

**DiskManager**
- File-backed persistent page storage
- Fixed-size 4096-byte pages
- Page allocation
- Full-page reads and writes
- Serialized file I/O

**HeapPage**
- Slotted-page layout for variable-length records
- Stable slot IDs
- Record insertion, lookup, deletion, and update
- Deleted-slot reuse
- Page compaction
- Free-space tracking

**PageDirectory**
- Persistent metadata describing HeapFile data pages
- Cached free-space information
- Linked directory pages for scalable HeapFile growth

**HeapFile**
- Persistent collection of HeapPages
- Record-level CRUD through RIDs
- Data-page selection during insertion
- Persistent page ownership
- Directory free-space synchronization
- Directory-page chaining
- Reopening existing heap files from persistent metadata

Records are identified using:

```cpp
struct RID {
    int page_id;
    std::uint16_t slot_id;
};
```

---

## Buffer Pool

The `BufferPoolManager` sits between higher-level storage components and physical disk I/O.

It currently supports:

- Page fetching
- Page allocation
- Pinning and unpinning
- Dirty-page tracking
- Explicit page flushing
- Flushing all resident dirty pages
- CLOCK-based replacement
- Dirty-page write-back during eviction
- Stable frame ownership
- Concurrent page access
- Reader/writer page latching
- Concurrent eviction and reload

The buffer pool uses a page table to map:

```text
page_id -> frame_id
```

and currently uses the CLOCK replacement algorithm when no empty frame is available.

---

## Concurrency Model

ExploreDB currently uses two levels of synchronization.

The `BufferPoolManager` uses a mutex to protect shared buffer-pool metadata such as:

- page-table mappings
- frame assignment
- pin counts
- dirty state
- replacement-policy state

Each `Page` owns a `std::shared_mutex`, allowing:

- multiple concurrent readers
- one exclusive writer

Page latches are managed with RAII using `std::shared_lock` and `std::unique_lock`.

The current lock-ordering rule is:

```text
BufferPoolManager coordination
        ↓
Page latch
```

Higher-level storage components are responsible for pinning pages through the buffer pool and acquiring the appropriate page latch before accessing page contents.

---

## Persistent Storage

ExploreDB uses a real file-backed storage layer.

The database file is treated as a sequence of fixed-size pages:

```text
page 0: bytes [0, 4095]
page 1: bytes [4096, 8191]
page 2: bytes [8192, 12287]
...
```

Page IDs currently map directly to physical page offsets:

```text
offset = page_id * PAGE_SIZE
```

Pages are cached in memory by the buffer pool and written back when explicitly flushed or when dirty pages are evicted.

The engine does not yet provide crash recovery or full transactional durability.

---

## Testing

The project uses GoogleTest and includes unit, integration, persistence, and concurrency testing.

Current coverage includes:

- DiskManager page allocation and persistence
- Invalid and misaligned file handling
- HeapPage record insertion, deletion, update, and compaction
- Slot reuse
- HeapFile CRUD
- Record persistence across buffer-pool recreation
- Page-directory metadata synchronization
- Directory-page growth and chaining
- Buffer-pool eviction
- Dirty-page write-back
- Concurrent fetches and writes
- Concurrent eviction
- Randomized multi-threaded stress testing
- ThreadSanitizer testing for data races

Correctness tests intentionally exercise very small buffer pools to force eviction and expose pinning or lifetime errors.

---

## Building

ExploreDB uses CMake.

```bash
cmake -S . -B build
cmake --build build
```

To build the test suite:

```bash
cmake --build build --target db_tests
```

Run all tests:

```bash
./build/db_tests
```

Run a specific GoogleTest suite:

```bash
./build/db_tests --gtest_filter=HeapFileTest.*
```

---

## Documentation

Design documentation lives under [`docs/`](docs/).

The documentation is split between the high-level system architecture and detailed component designs.

```text
docs/
├── Architecture.md
├── Technical_Debt.md
├── buffer/
└── storage/
```

[`docs/Architecture.md`](docs/Architecture.md) describes the overall system structure and component boundaries.

[`docs/Technical_Debt.md`](docs/Technical_Debt.md) tracks known limitations, unresolved design questions, and future architectural work.

Individual component documents describe their responsibilities, invariants, concurrency assumptions, and current limitations.

---

## Experimental Direction

ExploreDB is intended to evolve beyond implementing one fixed version of each database algorithm.

Where useful, major policies will become interchangeable so they can be evaluated under the same workloads.

### Buffer Replacement

The current implementation uses CLOCK.

Planned experiments include:

- CLOCK
- LRU
- LRU-K
- ARC
- potentially additional replacement policies

Example workloads may include:

- uniform random accesses
- hot/cold skew
- sequential scans
- mixed sequential and frequently reused pages
- varying working-set sizes
- varying buffer-pool capacities

Metrics may include:

- cache hit and miss rates
- eviction counts
- disk reads and writes
- dirty-page flushes
- latency
- throughput
- replacement-policy CPU overhead
- metadata memory overhead

The goal is not simply to determine which algorithm is "best," but to understand **which workloads favor each algorithm and why**.

---

## Python Benchmarking

Benchmarking is intended to become a first-class part of ExploreDB.

C++ will implement and execute the database algorithms, while Python will be used for benchmark orchestration, analysis, and visualization.

The expected workflow is:

```text
C++ database engine
        ↓
benchmark / workload runner
        ↓
structured metrics
        ↓
Python
        ↓
pandas / NumPy / Matplotlib
        ↓
analysis and visualization
```

Future experiments will extend beyond buffer replacement into areas such as:

- external sorting
- replacement selection
- join algorithms
- indexing strategies
- storage-management policies
- query execution techniques

---

## Roadmap

The current focus is the storage engine and experimental infrastructure.

Planned areas include:

- [x] File-backed DiskManager
- [x] BufferPoolManager
- [x] CLOCK replacement
- [x] Concurrent page access and latching
- [x] Slotted HeapPages
- [x] Persistent HeapFiles
- [x] PageDirectory and free-space metadata
- [x] Directory-page chaining
- [ ] Benchmark instrumentation
- [ ] Python benchmark and visualization pipeline
- [ ] Pluggable replacement-policy interface
- [ ] LRU replacement
- [ ] LRU-K replacement
- [ ] Additional replacement-policy experiments
- [ ] B+ tree indexing
- [ ] External sorting
- [ ] Relational execution operators
- [ ] Join algorithms
- [ ] SQL parsing and semantic analysis
- [ ] Query planning and optimization
- [ ] Transactions and concurrency control
- [ ] Write-ahead logging and recovery

The roadmap is intentionally iterative. New abstractions are introduced when concrete implementations demonstrate a useful shared boundary rather than being designed prematurely.

---

## Long-Term Vision

ExploreDB is intended to become an **interactive database-internals laboratory**.

A future interface may allow users to configure engine behavior before executing workloads, for example:

```sql
SET eviction_policy = 'lru_k';
SET buffer_pool_size = 128;
```

The same workload could then be executed under different configurations and compared using internal engine metrics and Python-generated analysis.

This could make ExploreDB useful for:

- learning database internals
- university database-systems courses
- experimenting with database algorithms
- visualizing systems tradeoffs
- benchmarking alternative implementations
- practicing systems programming and database engineering

The project prioritizes understanding, observability, and experimentation over production deployment.

---

## License

ExploreDB is licensed under the [MIT License](LICENSE).