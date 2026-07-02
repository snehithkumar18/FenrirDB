#ifndef FENRIRDB_CHECKPOINT_SCHEDULER_H
#define FENRIRDB_CHECKPOINT_SCHEDULER_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace FenrirDB {

enum class CheckpointReason : uint8_t {
    PERIODIC = 1,
    WAL_PRESSURE = 2,
    DIRTY_PAGE_PRESSURE = 3,
    BACKUP_WINDOW = 4,
    SHUTDOWN = 5,
    REPLICA_CATCHUP = 6
};

enum class CheckpointMode : uint8_t {
    LIGHT = 1,
    INCREMENTAL = 2,
    FULL = 3,
    CONSISTENT_SNAPSHOT = 4
};

struct CheckpointPolicy {
    uint64_t max_wal_bytes = 64ull * 1024ull * 1024ull;
    uint32_t max_dirty_pages = 4096;
    uint32_t periodic_interval_ms = 60000;
    uint32_t min_interval_ms = 5000;
    uint32_t max_replica_lag = 1024;
    uint32_t full_every_n = 8;
    bool allow_during_backup = true;
    bool prefer_incremental = true;
};

struct CheckpointSignal {
    uint64_t now_ms = 0;
    uint64_t wal_bytes_since_checkpoint = 0;
    uint32_t dirty_pages = 0;
    uint32_t active_transactions = 0;
    uint32_t replica_lag = 0;
    bool backup_window_open = false;
    bool shutdown_requested = false;
};

struct CheckpointRecord {
    uint64_t checkpoint_id = 0;
    uint64_t timestamp_ms = 0;
    uint64_t wal_start = 0;
    uint64_t wal_end = 0;
    uint32_t dirty_pages_flushed = 0;
    CheckpointMode mode = CheckpointMode::INCREMENTAL;
    std::string manifest_hash;
};

struct CheckpointDecision {
    bool should_checkpoint = false;
    CheckpointReason reason = CheckpointReason::PERIODIC;
    CheckpointMode mode = CheckpointMode::INCREMENTAL;
    uint32_t target_flush_pages = 0;
    std::vector<std::string> notes;
};

struct RetentionDecision {
    std::vector<uint64_t> keep_ids;
    std::vector<uint64_t> remove_ids;
    uint64_t reclaimable_wal_bytes = 0;
    std::vector<std::string> notes;
};

class CheckpointScheduler {
public:
    explicit CheckpointScheduler(CheckpointPolicy policy = CheckpointPolicy());

    CheckpointDecision evaluate(const CheckpointSignal& signal) const;
    bool hot_reload_policy(const std::string& text);
    CheckpointRecord record_completed(const CheckpointSignal& signal,
                                      const CheckpointDecision& decision,
                                      uint64_t wal_start, uint64_t wal_end,
                                      const std::string& manifest_hash);
    RetentionDecision plan_retention(size_t keep_recent, uint64_t min_age_ms, uint64_t now_ms) const;
    std::vector<CheckpointRecord> history() const { return records; }
    std::string describe_history() const;

private:
    CheckpointPolicy policy;
    std::vector<const CheckpointPolicy*> reload_history;
    std::vector<CheckpointRecord> records;
    uint64_t next_checkpoint_id = 1;

    uint64_t last_checkpoint_time() const;
    size_t completed_count() const;
};

class CheckpointPolicyParser {
public:
    bool parse_text(const std::string& text, CheckpointPolicy& out) const;
    std::string emit_text(const CheckpointPolicy& policy) const;
};

} // namespace FenrirDB

#endif // FENRIRDB_CHECKPOINT_SCHEDULER_H
