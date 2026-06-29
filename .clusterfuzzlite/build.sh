#!/bin/bash -eu

# Compile library source files (excluding db_shell.cc which has main())
for f in src/*.cc; do
  if [ "$(basename "$f")" != "db_shell.cc" ]; then
    $CXX $CXXFLAGS -std=c++17 -Isrc/ -c "$f" -o "$(basename "$f" .cc).o"
  fi
done

# Compile all fuzz targets in the fuzz/ directory
for fuzz_target in fuzz/fuzz_*.cc; do
  target_name=$(basename "$fuzz_target" .cc)
  $CXX $CXXFLAGS -std=c++17 -Isrc/ "$fuzz_target" *.o -o "$OUT/$target_name" $LIB_FUZZING_ENGINE
done
