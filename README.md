# FenrirDB: Stateful Document Database Engine

FenrirDB is a lightweight, page-based C++ NoSQL document database engine. It stores documents in a slotted-page binary file format, indexes keys using an on-disk B+ Tree, and optimizes I/O queries using an LRU Page Cache (Buffer Pool Manager).

## Key Features

1. **Slotted-Page Storage**: Implements variable-length record management inside fixed-size (4096 bytes) storage pages. Supports record compaction to reclaim fragmented space.
2. **Buffer Pool Manager (LRU Cache)**: Retains active pages in memory to reduce physical disk reads. Implements LRU-based eviction to recycle pool slots.
3. **B+ Tree Indexing**: Implements leaf-based index structures for O(log N) key lookups and sorted range searches.
4. **JSON Variant Evaluator**: Evaluates simple comparison query expressions against document fields (supports String, Int, and Boolean types).

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
│   └── database.h/.cc    # Main database API
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
