# FenrirDB: Stateful Document Database Engine

FenrirDB is a lightweight, page-based C++ NoSQL document database engine. It stores documents in a slotted-page binary file format, indexes keys using an on-disk B+ Tree, and optimizes I/O queries using an LRU Page Cache (Buffer Pool Manager).

## Key Features

1. **Slotted-Page Storage**: Implements variable-length record management inside fixed-size (4096 bytes) storage pages. Supports record compaction to reclaim fragmented space.
2. **Buffer Pool Manager (LRU Cache)**: Retains active pages in memory to reduce physical disk reads. Implements LRU-based eviction to recycle pool slots.
3. **B+ Tree Indexing**: Implements leaf-based index structures for O(log N) key lookups, recursive node splits, and sorted range searches.
4. **JSON Document Parser**: Supports fully nested JSON objects and arrays using a custom token scanner, recursive-descent parser, and variant serialization formats.
5. **Volcano Query Execution**: Implements logical SQL AST parsing, logical plan compilations, and sequential / B+ Tree index scan pipeline operators.
6. **Transaction Manager (MVCC)**: Supports transaction lifecycles, Read Timestamp version isolation (MVCC snapshot visibility), and write locks.
7. **Write-Ahead Logging (WAL) & Recovery**: Appends transactional mutations to binary WAL files and executes analysis, Redo, and Undo passes for robust crash recovery.

## Directory Structure

```
fenrirdb/
├── src/
│   ├── errors.h          # Error codes
│   ├── logger.h          # Logging system
│   ├── storage.h/.cc     # Slotted-page memory & disk management
│   ├── cache.h/.cc       # LRU page cache buffer pool
│   ├── index.h/.cc       # On-disk B+ Tree indexing
│   ├── query.h/.cc       # Variant fields & AST Query evaluator
│   ├── sql_parser.h/.cc  # SQL Query lexer and parser
│   ├── json_parser.h/.cc # JSON Parser and Serializer
│   ├── query_planner.h/  # Volcano planner and executors
│   ├── wal.h/.cc         # Write-Ahead Logging & recovery
│   ├── lock_manager.h/   # Concurrency Lock Manager (2PL)
│   ├── db_shell.cc       # Interactive shell interface
│   └── database.h/.cc    # Main database API
├── tests/
│   ├── test_storage.cc   # Storage unit tests
│   ├── test_cache.cc     # Cache unit tests
│   ├── test_index.cc     # B+ Tree split tests
│   ├── test_lock.cc      # Concurrency lock tests
│   ├── test_wal.cc       # Transaction rollback tests
│   ├── test_json.cc      # Nested document serialization tests
│   ├── test_planner.cc   # Volcano executors tests
│   ├── test_tx.cc        # MVCC read isolation tests
│   └── test_stress.cc    # Multi-threaded concurrent workload stress run
├── fuzz/
│   ├── fuzz_query.cc     # Harness for fuzzer queries
│   ├── fuzz_storage.cc   # Harness for fuzzer storage/compaction
│   └── fuzzer_main.cc    # Standalone harness test runner
├── .clusterfuzzlite/
│   ├── project.yaml      # Language declaration
│   └── build.sh          # ClusterFuzz build configuration
└── CMakeLists.txt        # Local build configuration
```

## Compilation

Build locally using CMake:
```bash
mkdir build && cd build
cmake ..
cmake --build .
```
