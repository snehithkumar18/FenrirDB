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
)

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
