#!/bin/bash -eu

# Compile library source files
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/storage.cc -o storage.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/cache.cc -o cache.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/index.cc -o index.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/query.cc -o query.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/database.cc -o database.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/wal.cc -o wal.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/lock_manager.cc -o lock_manager.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/sql_parser.cc -o sql_parser.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/json_parser.cc -o json_parser.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/query_planner.cc -o query_planner.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/transaction_manager.cc -o transaction_manager.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/optimizer.cc -o optimizer.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/checkpoint.cc -o checkpoint.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/wal_buffer.cc -o wal_buffer.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/query_engine_compiler.cc -o query_engine_compiler.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/query_engine_compiler_pass.cc -o query_engine_compiler_pass.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/query_engine_compiler_window.cc -o query_engine_compiler_window.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/query_engine_compiler_window_agg.cc -o query_engine_compiler_window_agg.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/query_engine_compiler_window_ranking.cc -o query_engine_compiler_window_ranking.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/query_engine_compiler_recursive.cc -o query_engine_compiler_recursive.o

# Compile fuzz targets and link with the fuzzing engine
$CXX $CXXFLAGS -std=c++17 -Isrc/ fuzz/fuzz_query.cc storage.o cache.o index.o query.o database.o wal.o lock_manager.o sql_parser.o json_parser.o query_planner.o transaction_manager.o optimizer.o checkpoint.o wal_buffer.o query_engine_compiler.o query_engine_compiler_pass.o query_engine_compiler_window.o query_engine_compiler_window_agg.o query_engine_compiler_window_ranking.o query_engine_compiler_recursive.o -o $OUT/fuzz_query $LIB_FUZZING_ENGINE
$CXX $CXXFLAGS -std=c++17 -Isrc/ fuzz/fuzz_storage.cc storage.o cache.o index.o query.o database.o wal.o lock_manager.o sql_parser.o json_parser.o query_planner.o transaction_manager.o optimizer.o checkpoint.o wal_buffer.o query_engine_compiler.o query_engine_compiler_pass.o query_engine_compiler_window.o query_engine_compiler_window_agg.o query_engine_compiler_window_ranking.o query_engine_compiler_recursive.o -o $OUT/fuzz_storage $LIB_FUZZING_ENGINE
