cmake_minimum_required(VERSION 3.12)
project(FenrirDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

# Enable AddressSanitizer for memory safety bug validation
if(MSVC)
    add_compile_options(/fsanitize=address /Z7 /EHsc)
else()
    add_compile_options(-fsanitize=address -g -O1)
    add_link_options(-fsanitize=address)
endif()

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
    src/query_engine_compiler.cc
    src/query_engine_compiler_pass.cc
    src/query_engine_compiler_window.cc
    src/query_engine_compiler_window_agg.cc
    src/query_engine_compiler_window_ranking.cc
    src/query_engine_compiler_recursive.cc
    src/query_engine_compiler_pivot.cc
    src/query_engine_compiler_partition.cc
    src/query_engine_compiler_partition_hash.cc
    src/query_engine_compiler_partition_list.cc
    src/query_engine_compiler_pivot_unpivot.cc
    src/bloom_filter.cc
    src/database_backup.cc
    src/hash_index.cc
    src/json_schema.cc
    src/lock_escalation.cc
    src/lock_manager_advanced.cc
    src/optimizer_cbo.cc
    src/optimizer_cbo_rules.cc
    src/optimizer_cbo_stats.cc
    src/optimizer_cbo_stats_histogram.cc
    src/query_engine_compiler_partition_optimizer.cc
    src/query_planner_ast.cc
    src/query_planner_ast_cbo.cc
    src/query_planner_decorrelate.cc
    src/query_planner_explain.cc
    src/sql_parser_advanced.cc
    src/storage_compressor.cc
    src/storage_compressor_huffman.cc
    src/storage_compressor_lzw.cc
    src/statistics.cc
    src/transaction_isolation.cc
    src/transaction_manager_savepoint.cc
    src/vectorized_executor.cc
    src/vectorized_executor_advanced.cc
    src/catalog.cc
    src/workload_planner.cc
    src/schema_evolution.cc
    src/replication_topology.cc
    src/checkpoint_scheduler.cc
    src/backup_restore.cc
    src/telemetry.cc
    src/binary_stream.cc
    src/batch_job_runner.cc
    src/data_transform.cc
    src/runtime_expression.cc
    src/runtime_manifest.cc
    src/runtime_pipeline.cc
    src/segment_cache.cc
    src/workload_codec.cc
)

# Build the main database library
add_library(fenrirdb_lib STATIC ${DB_SOURCES})

# Core Shell executable
add_executable(db_shell src/db_shell.cc)
target_link_libraries(db_shell fenrirdb_lib)

# Test suite targets
set(TEST_FILES
    tests/test_storage.cc
    tests/test_cache.cc
    tests/test_index.cc
    tests/test_database.cc
    tests/test_lock_manager.cc
    tests/test_wal.cc
    tests/test_json_parser.cc
    tests/test_query_planner.cc
    tests/test_transaction_manager.cc
    tests/test_optimizer.cc
    tests/test_nesting_stress.cc
    tests/test_joins.cc
    tests/test_aggregations.cc
    tests/test_checkpoint.cc
    tests/test_composite_index.cc
    tests/test_cbo_regression.cc
    tests/test_executors_advanced.cc
    tests/test_wal_buffer.cc
    tests/test_parser_stress.cc
    tests/test_mct_stress.cc
    tests/test_compiler.cc
    tests/test_compiler_pass.cc
    tests/test_window.cc
    tests/test_window_agg.cc
    tests/test_window_ranking.cc
    tests/test_recursive_cte.cc
    tests/test_pivot.cc
    tests/test_partition.cc
    tests/test_partition_hash.cc
    tests/test_partition_list.cc
    tests/test_unpivot.cc
    tests/test_sql_cte_joins.cc
    tests/test_runtime_subsystems.cc
    tests/test_catalog.cc
    tests/test_workload_planner.cc
    tests/test_schema_evolution.cc
    tests/test_replication_topology.cc
    tests/test_checkpoint_scheduler.cc
    tests/test_backup_restore.cc
    tests/test_telemetry.cc
    tests/test_binary_stream.cc
    tests/test_batch_job_runner.cc
    tests/test_data_transform.cc
)

foreach(TEST_SRC ${TEST_FILES})
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_link_libraries(${TEST_NAME} fenrirdb_lib)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

set(STRESS_TOOLS
    tests/benchmark.cc
    tests/test_stress.cc
    tests/test_recovery_stress.cc
    tests/test_composite_fuzz.cc
    tests/test_performance_stress.cc
)

foreach(TOOL_SRC ${STRESS_TOOLS})
    get_filename_component(TOOL_NAME ${TOOL_SRC} NAME_WE)
    add_executable(${TOOL_NAME} ${TOOL_SRC})
    target_link_libraries(${TOOL_NAME} fenrirdb_lib)
endforeach()

# Fuzzers build configuration
file(GLOB FENRIR_FUZZERS CONFIGURE_DEPENDS fuzz/fuzz_*.cc)
foreach(FUZZ_SRC ${FENRIR_FUZZERS})
    get_filename_component(FUZZ_NAME ${FUZZ_SRC} NAME_WE)
    add_executable(${FUZZ_NAME} ${FUZZ_SRC})
    target_link_libraries(${FUZZ_NAME} fenrirdb_lib)
    if(DEFINED ENV{LIB_FUZZING_ENGINE})
        target_link_libraries(${FUZZ_NAME} $ENV{LIB_FUZZING_ENGINE})
    else()
        target_compile_definitions(${FUZZ_NAME} PRIVATE STANDALONE_FUZZER)
        target_sources(${FUZZ_NAME} PRIVATE fuzz/fuzzer_main.cc)
    endif()
endforeach()
