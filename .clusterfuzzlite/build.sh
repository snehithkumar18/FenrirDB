#!/bin/bash -eu

SRC="${SRC:-$(pwd)}"
OUT="${OUT:-$SRC/out}"
CXX="${CXX:-clang++}"
CXXFLAGS="${CXXFLAGS:--fsanitize=address,undefined,fuzzer-no-link -g -O1}"
LIB_FUZZING_ENGINE="${LIB_FUZZING_ENGINE:--fsanitize=fuzzer}"
mkdir -p "$OUT"

CORE_SOURCES=(
  backup_restore
  batch_job_runner
  binary_stream
  bloom_filter
  cache
  catalog
  checkpoint
  checkpoint_scheduler
  data_transform
  database
  database_backup
  hash_index
  index
  json_parser
  json_schema
  lock_escalation
  lock_manager
  lock_manager_advanced
  optimizer
  optimizer_cbo
  optimizer_cbo_rules
  optimizer_cbo_stats
  optimizer_cbo_stats_histogram
  query
  query_engine_compiler
  query_engine_compiler_partition
  query_engine_compiler_partition_hash
  query_engine_compiler_partition_list
  query_engine_compiler_partition_optimizer
  query_engine_compiler_pass
  query_engine_compiler_pivot
  query_engine_compiler_pivot_unpivot
  query_engine_compiler_recursive
  query_engine_compiler_window
  query_engine_compiler_window_agg
  query_engine_compiler_window_ranking
  query_planner
  query_planner_ast
  query_planner_ast_cbo
  query_planner_decorrelate
  query_planner_explain
  replication_topology
  runtime_expression
  runtime_manifest
  runtime_pipeline
  schema_evolution
  segment_cache
  sql_parser
  sql_parser_advanced
  statistics
  storage
  storage_compressor
  storage_compressor_huffman
  storage_compressor_lzw
  telemetry
  transaction_isolation
  transaction_manager
  transaction_manager_savepoint
  vectorized_executor
  vectorized_executor_advanced
  wal
  wal_buffer
  workload_codec
  workload_planner
)

FUZZ_TARGETS=(
  fuzz_backup_restore
  fuzz_batch_job_runner
  fuzz_binary_stream
  fuzz_cbo
  fuzz_catalog_planner
  fuzz_checkpoint_scheduler
  fuzz_data_transform
  fuzz_expression
  fuzz_huffman
  fuzz_lock
  fuzz_lzw
  fuzz_manifest
  fuzz_pipeline
  fuzz_query
  fuzz_replication_topology
  fuzz_schema_evolution
  fuzz_segment_cache
  fuzz_storage
  fuzz_telemetry
  fuzz_tx
  fuzz_vectorized
  fuzz_workload
)

ALL_OBJS=()
for unit in "${CORE_SOURCES[@]}"; do
  src_file="$SRC/src/${unit}.cc"
  obj_file="$OUT/${unit}.o"
  if [[ ! -f "$src_file" ]]; then
    echo "Missing core source: $src_file" >&2
    exit 1
  fi
  "$CXX" $CXXFLAGS -std=c++17 -I"$SRC/src" -c "$src_file" -o "$obj_file"
  ALL_OBJS+=("$obj_file")
done

for target in "${FUZZ_TARGETS[@]}"; do
  fuzz_src="$SRC/fuzz/${target}.cc"
  if [[ ! -f "$fuzz_src" ]]; then
    echo "Missing fuzz target source: $fuzz_src" >&2
    exit 1
  fi
  "$CXX" $CXXFLAGS -std=c++17 -I"$SRC/src" "$fuzz_src" "${ALL_OBJS[@]}" \
    -o "$OUT/$target" $LIB_FUZZING_ENGINE
done
