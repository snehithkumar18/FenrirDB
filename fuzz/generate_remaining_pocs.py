#!/usr/bin/env python3
import os
import struct

os.makedirs("fuzz/pocs", exist_ok=True)

# 1. Bug 12: Waits-For Graph Deadlock UAF (fuzz_lock)
# We generate a sequence of operations that repeatedly acquires and releases locks
# on multiple resources with different transactions to trigger Waits-For cycle detection
# and concurrent queue modification.
# Consumed: op (1B), resource (1B) -> "res_%d", mode (1B) -> total 3 bytes per operation.
# We need at least 10 bytes to start. Let's write 60 operations (180 bytes).
lock_data = bytearray()
for i in range(60):
    op = i % 4
    res = i % 5
    mode = i % 5
    lock_data.extend([op, res, mode])

with open("fuzz/pocs/poc_lock_deadlock_uaf.bin", "wb") as f:
    f.write(lock_data)

# 2. Bug 6: Schema Evolution Vector Iterator UAF (fuzz_schema_evolution)
# We need a schema with name/field, followed by a step that renames a field,
# followed by multiple add_field actions to trigger vector reallocation.
schema_evolution_data = (
    "table users\n"
    "version 1\n"
    "field id int required\n"
    "field name string\n"
    "---\n"
    "step users 1 2\n"
    "action rename_field name first_name\n"
    "action add_field email string\n"
    "action add_field age int\n"
    "action add_field status string\n"
    "action add_field score int\n"
    "action add_field active bool\n"
    "action add_field phone string\n"
)

with open("fuzz/pocs/poc_schema_evolution_uaf.txt", "wb") as f:
    f.write(schema_evolution_data.encode("utf-8"))

# 3. Bug 16: Checkpoint Policy Hot-Reload UAF (fuzz_checkpoint_scheduler)
# We need policy text containing full_every_n=0.
checkpoint_data = (
    "max_wal_bytes=1024\n"
    "max_dirty_pages=100\n"
    "periodic_interval_ms=5000\n"
    "min_interval_ms=100\n"
    "max_replica_lag=50\n"
    "full_every_n=0\n"
    "allow_during_backup=true\n"
    "prefer_incremental=true\n"
)

with open("fuzz/pocs/poc_checkpoint_policy_uaf.txt", "wb") as f:
    f.write(checkpoint_data.encode("utf-8"))

# 4. Bug 11: Pipeline Window Out-of-Bounds (fuzz_pipeline)
# We need a pipeline stage string containing a window operation to trigger the OOB read/write.
# And we want to project with alias that exceeds length or a long list of fields to overflow.
# Format: Load table, then window with Group and width, then project.
pipeline_data = (
    "load|users\n"
    "window|group|rows|10\n"
    "project|id:very_long_alias_name_to_overflow_scratch_vector_buffer_size\n"
)

with open("fuzz/pocs/poc_pipeline_window_oob.txt", "wb") as f:
    f.write(pipeline_data.encode("utf-8"))

# 5. Bug 10: Batch Task Retry Realloc UAF (fuzz_batch_job_runner)
# Triggers batch job execution retry logic.
batch_data = (
    "table users\n"
    "version 1\n"
    "field id int required unique\n"
    "---\n"
    "job nightly\n"
    "task t1 plan_workload cost=1 workload=find|users\n"
)

with open("fuzz/pocs/poc_batch_task_retry_uaf.txt", "wb") as f:
    f.write(batch_data.encode("utf-8"))

print("Generated remaining PoC files successfully in fuzz/pocs/.")
