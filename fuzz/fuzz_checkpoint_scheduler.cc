#include "../src/checkpoint_scheduler.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 32) return 0;

    FenrirDB::CheckpointPolicy policy;
    FenrirDB::CheckpointPolicyParser parser;
    std::string text(reinterpret_cast<const char*>(data), size);
    parser.parse_text(text, policy);

    FenrirDB::CheckpointScheduler scheduler(policy);
    scheduler.hot_reload_policy(text);
    size_t offset = 0;
    for (int i = 0; i < 8 && offset + 24 <= size; ++i) {
        FenrirDB::CheckpointSignal signal;
        std::memcpy(&signal.now_ms, data + offset, 8);
        offset += 8;
        std::memcpy(&signal.wal_bytes_since_checkpoint, data + offset, 8);
        offset += 8;
        signal.dirty_pages = data[offset] * 16u;
        signal.active_transactions = data[offset + 1];
        signal.replica_lag = data[offset + 2] * 8u;
        signal.backup_window_open = (data[offset + 3] & 1) != 0;
        signal.shutdown_requested = (data[offset + 3] & 2) != 0;
        offset += 8;

        FenrirDB::CheckpointDecision decision = scheduler.evaluate(signal);
        if (decision.should_checkpoint) {
            scheduler.record_completed(signal, decision,
                                       signal.wal_bytes_since_checkpoint / 2,
                                       signal.wal_bytes_since_checkpoint,
                                       "fuzz");
        }
    }
    (void)scheduler.plan_retention((size % 4) + 1, 1000, UINT64_MAX / 4);
    (void)scheduler.describe_history();
    return 0;
}
