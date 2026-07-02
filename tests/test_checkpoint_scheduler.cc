#include "../src/checkpoint_scheduler.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    FenrirDB::CheckpointPolicyParser parser;
    FenrirDB::CheckpointPolicy policy;
    assert(parser.parse_text(
        "max_wal_bytes=1024\n"
        "max_dirty_pages=100\n"
        "periodic_interval_ms=5000\n"
        "min_interval_ms=100\n"
        "max_replica_lag=50\n"
        "full_every_n=2\n",
        policy));

    FenrirDB::CheckpointScheduler scheduler(policy);
    FenrirDB::CheckpointSignal signal;
    signal.now_ms = 1000;
    signal.wal_bytes_since_checkpoint = 2048;
    signal.dirty_pages = 75;

    FenrirDB::CheckpointDecision decision = scheduler.evaluate(signal);
    assert(decision.should_checkpoint);
    assert(decision.reason == FenrirDB::CheckpointReason::WAL_PRESSURE);
    assert(decision.target_flush_pages > 0);

    auto record = scheduler.record_completed(signal, decision, 0, 2048, "manifest-a");
    assert(record.checkpoint_id == 1);

    signal.now_ms = 7000;
    signal.backup_window_open = true;
    signal.wal_bytes_since_checkpoint = 1;
    signal.dirty_pages = 10;
    decision = scheduler.evaluate(signal);
    assert(decision.should_checkpoint);
    assert(decision.mode == FenrirDB::CheckpointMode::CONSISTENT_SNAPSHOT ||
           decision.mode == FenrirDB::CheckpointMode::FULL);
    scheduler.record_completed(signal, decision, 2048, 4096, "manifest-b");

    auto retention = scheduler.plan_retention(1, 1000, 10000);
    assert(!retention.keep_ids.empty());
    assert(scheduler.describe_history().find("checkpoint") != std::string::npos);
    assert(parser.emit_text(policy).find("max_wal_bytes") != std::string::npos);

    std::cout << "checkpoint scheduler tests passed\n";
    return 0;
}
