#!/bin/bash -eu

# Compile library source files
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/storage.cc -o storage.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/cache.cc -o cache.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/index.cc -o index.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/query.cc -o query.o
$CXX $CXXFLAGS -std=c++17 -Isrc/ -c src/database.cc -o database.o

# Compile fuzz targets and link with the fuzzing engine
$CXX $CXXFLAGS -std=c++17 -Isrc/ fuzz/fuzz_query.cc storage.o cache.o index.o query.o database.o -o $OUT/fuzz_query $LIB_FUZZING_ENGINE
$CXX $CXXFLAGS -std=c++17 -Isrc/ fuzz/fuzz_storage.cc storage.o cache.o index.o query.o database.o -o $OUT/fuzz_storage $LIB_FUZZING_ENGINE
