cmake_minimum_required(VERSION 3.12)
project(FenrirDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable AddressSanitizer for memory safety bug validation
add_compile_options(-fsanitize=address -g -O1)
add_link_options(-fsanitize=address)

include_directories(src)

set(DB_SOURCES
    src/storage.cc
    src/cache.cc
    src/index.cc
    src/query.cc
    src/database.cc
    src/wal.cc
    src/lock_manager.cc
    src/sql_parser.cc
    src/json_parser.cc
    src/query_planner.cc
    src/transaction_manager.cc
    src/optimizer.cc
    src/checkpoint.cc
    src/wal_buffer.cc
)

# Core Shell executable
add_executable(db_shell src/db_shell.cc ${DB_SOURCES})

# Test suite targets
add_executable(test_storage tests/test_storage.cc ${DB_SOURCES})
add_executable(test_cache tests/test_cache.cc ${DB_SOURCES})
add_executable(test_index tests/test_index.cc ${DB_SOURCES})
add_executable(test_database tests/test_database.cc ${DB_SOURCES})
add_executable(test_lock_manager tests/test_lock_manager.cc ${DB_SOURCES})
add_executable(test_wal tests/test_wal.cc ${DB_SOURCES})
add_executable(test_stress tests/test_stress.cc ${DB_SOURCES})
add_executable(test_json_parser tests/test_json_parser.cc ${DB_SOURCES})
add_executable(test_query_planner tests/test_query_planner.cc ${DB_SOURCES})
add_executable(test_transaction_manager tests/test_transaction_manager.cc ${DB_SOURCES})
add_executable(test_optimizer tests/test_optimizer.cc ${DB_SOURCES})
add_executable(test_benchmark tests/benchmark.cc ${DB_SOURCES})
add_executable(test_recovery_stress tests/test_recovery_stress.cc ${DB_SOURCES})
add_executable(test_nesting_stress tests/test_nesting_stress.cc ${DB_SOURCES})
add_executable(test_joins tests/test_joins.cc ${DB_SOURCES})
add_executable(test_aggregations tests/test_aggregations.cc ${DB_SOURCES})
add_executable(test_checkpoint tests/test_checkpoint.cc ${DB_SOURCES})
add_executable(test_composite_index tests/test_composite_index.cc ${DB_SOURCES})
add_executable(test_cbo_crashes tests/test_cbo_crashes.cc ${DB_SOURCES})
add_executable(test_composite_fuzz tests/test_composite_fuzz.cc ${DB_SOURCES})
add_executable(test_executors_advanced tests/test_executors_advanced.cc ${DB_SOURCES})
add_executable(test_wal_buffer tests/test_wal_buffer.cc ${DB_SOURCES})
add_executable(test_performance_stress tests/test_performance_stress.cc ${DB_SOURCES})
add_executable(test_parser_stress tests/test_parser_stress.cc ${DB_SOURCES})
add_executable(test_mct_stress tests/test_mct_stress.cc ${DB_SOURCES})

if(DEFINED ENV{LIB_FUZZING_ENGINE})
    # Platform build mode
    add_executable(fuzz_query fuzz/fuzz_query.cc ${DB_SOURCES})
    target_link_libraries(fuzz_query $ENV{LIB_FUZZING_ENGINE})

    add_executable(fuzz_storage fuzz/fuzz_storage.cc ${DB_SOURCES})
    target_link_libraries(fuzz_storage $ENV{LIB_FUZZING_ENGINE})
else()
    # Standalone local build mode
    add_executable(fuzz_query fuzz/fuzz_query.cc ${DB_SOURCES})
    target_compile_definitions(fuzz_query PRIVATE STANDALONE_FUZZER)
    target_sources(fuzz_query PRIVATE fuzz/fuzzer_main.cc)

    add_executable(fuzz_storage fuzz/fuzz_storage.cc ${DB_SOURCES})
    target_compile_definitions(fuzz_storage PRIVATE STANDALONE_FUZZER)
    target_sources(fuzz_storage PRIVATE fuzz/fuzzer_main.cc)
endif()
