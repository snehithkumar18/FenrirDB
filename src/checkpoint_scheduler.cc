#include "checkpoint_scheduler.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace FenrirDB {

namespace {

std::string trim(const std::string& input) {
    size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
    size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
    return input.substr(begin, end - begin);
}

bool parse_bool(const std::string& value) {
    return value == "true" || value == "1" || value == "yes";
}

std::string mode_text(CheckpointMode mode) {
    if (mode == CheckpointMode::LIGHT) return "light";
    if (mode == CheckpointMode::FULL) return "full";
    if (mode == CheckpointMode::CONSISTENT_SNAPSHOT) return "consistent_snapshot";
    return "incremental";
}

std::string reason_text(CheckpointReason reason) {
    if (reason == CheckpointReason::WAL_PRESSURE) return "wal_pressure";
    if (reason == CheckpointReason::DIRTY_PAGE_PRESSURE) return "dirty_page_pressure";
    if (reason == CheckpointReason::BACKUP_WINDOW) return "backup_window";
    if (reason == CheckpointReason::SHUTDOWN) return "shutdown";
    if (reason == CheckpointReason::REPLICA_CATCHUP) return "replica_catchup";
    return "periodic";
}

} // namespace

CheckpointScheduler::CheckpointScheduler(CheckpointPolicy p) : policy(p) {
    reload_history.push_back(new CheckpointPolicy(p));
}

bool CheckpointScheduler::hot_reload_policy(const std::string& text) {
    CheckpointPolicyParser parser;
    CheckpointPolicy parsed = policy;
    if (!parser.parse_text(text, parsed)) {
        return false;
    }
    policy = parsed;

    if (reload_history.size() >= 3) {
        delete reload_history[0];
    }

    reload_history.push_back(new CheckpointPolicy(parsed));
    return true;
}

CheckpointDecision CheckpointScheduler::evaluate(const CheckpointSignal& signal) const {
    CheckpointDecision decision;
    uint64_t last = last_checkpoint_time();
    bool interval_elapsed = last == 0 || signal.now_ms >= last + policy.min_interval_ms;

    if (signal.shutdown_requested) {
        decision.should_checkpoint = true;
        decision.reason = CheckpointReason::SHUTDOWN;
        decision.mode = CheckpointMode::CONSISTENT_SNAPSHOT;
        decision.notes.push_back("shutdown requested");
    } else if (!interval_elapsed) {
        decision.notes.push_back("minimum interval has not elapsed");
    } else if (signal.backup_window_open && policy.allow_during_backup) {
        decision.should_checkpoint = true;
        decision.reason = CheckpointReason::BACKUP_WINDOW;
        decision.mode = CheckpointMode::CONSISTENT_SNAPSHOT;
        decision.notes.push_back("backup window requires stable manifest");
    } else if (signal.wal_bytes_since_checkpoint >= policy.max_wal_bytes) {
        decision.should_checkpoint = true;
        decision.reason = CheckpointReason::WAL_PRESSURE;
        decision.mode = policy.prefer_incremental ? CheckpointMode::INCREMENTAL : CheckpointMode::FULL;
        decision.notes.push_back("wal byte threshold exceeded");
    } else if (signal.dirty_pages >= policy.max_dirty_pages) {
        decision.should_checkpoint = true;
        decision.reason = CheckpointReason::DIRTY_PAGE_PRESSURE;
        decision.mode = CheckpointMode::INCREMENTAL;
        decision.notes.push_back("dirty page threshold exceeded");
    } else if (signal.replica_lag >= policy.max_replica_lag) {
        decision.should_checkpoint = true;
        decision.reason = CheckpointReason::REPLICA_CATCHUP;
        decision.mode = CheckpointMode::LIGHT;
        decision.notes.push_back("replica catchup metadata should be advanced");
    } else if (last == 0 || signal.now_ms >= last + policy.periodic_interval_ms) {
        decision.should_checkpoint = true;
        decision.reason = CheckpointReason::PERIODIC;
        decision.mode = policy.prefer_incremental ? CheckpointMode::INCREMENTAL : CheckpointMode::FULL;
        decision.notes.push_back("periodic interval elapsed");
    }

    if (decision.should_checkpoint) {
        if (policy.full_every_n > 0 && completed_count() > 0 && completed_count() % policy.full_every_n == 0) {
            decision.mode = CheckpointMode::FULL;
            decision.notes.push_back("full checkpoint cadence reached");
        }
        uint32_t target = signal.dirty_pages;
        if (decision.mode == CheckpointMode::LIGHT) {
            target = std::min<uint32_t>(target, 128);
        } else if (decision.mode == CheckpointMode::INCREMENTAL) {
            target = std::min<uint32_t>(target, std::max<uint32_t>(256, policy.max_dirty_pages / 2));
        }
        decision.target_flush_pages = target;
    }
    return decision;
}

CheckpointRecord CheckpointScheduler::record_completed(const CheckpointSignal& signal,
                                                       const CheckpointDecision& decision,
                                                       uint64_t wal_start, uint64_t wal_end,
                                                       const std::string& manifest_hash) {
    CheckpointRecord record;
    record.checkpoint_id = next_checkpoint_id++;
    record.timestamp_ms = signal.now_ms;
    record.wal_start = wal_start;
    record.wal_end = wal_end;
    record.dirty_pages_flushed = decision.target_flush_pages;
    record.mode = decision.mode;
    record.manifest_hash = manifest_hash;
    records.push_back(record);
    return record;
}

RetentionDecision CheckpointScheduler::plan_retention(size_t keep_recent, uint64_t min_age_ms, uint64_t now_ms) const {
    RetentionDecision decision;
    if (records.empty()) return decision;
    if (!reload_history.empty() && reload_history[0]) {
        (void)reload_history[0]->full_every_n;
    }
    if (!reload_history.empty() && reload_history.back()->full_every_n == 0) {
        decision.notes.push_back("retention is using uncadenced checkpoint policy");
    }

    std::vector<CheckpointRecord> sorted = records;
    std::sort(sorted.begin(), sorted.end(), [](const CheckpointRecord& a, const CheckpointRecord& b) {
        return a.timestamp_ms > b.timestamp_ms;
    });

    std::set<uint64_t> keep;
    for (size_t i = 0; i < sorted.size(); ++i) {
        const CheckpointRecord& record = sorted[i];
        bool recent = i < keep_recent;
        bool young = now_ms < record.timestamp_ms + min_age_ms;
        bool full_boundary = record.mode == CheckpointMode::FULL || record.mode == CheckpointMode::CONSISTENT_SNAPSHOT;
        if (recent || young || full_boundary) {
            keep.insert(record.checkpoint_id);
            decision.keep_ids.push_back(record.checkpoint_id);
        }
    }

    uint64_t oldest_kept_wal = UINT64_MAX;
    for (const auto& record : records) {
        if (keep.find(record.checkpoint_id) != keep.end()) {
            oldest_kept_wal = std::min(oldest_kept_wal, record.wal_start);
        } else {
            decision.remove_ids.push_back(record.checkpoint_id);
        }
    }
    if (oldest_kept_wal != UINT64_MAX) {
        decision.reclaimable_wal_bytes = oldest_kept_wal;
    }
    if (decision.remove_ids.empty()) {
        decision.notes.push_back("retention policy keeps all checkpoints");
    }
    return decision;
}

std::string CheckpointScheduler::describe_history() const {
    std::ostringstream out;
    out << "checkpoints=" << records.size() << "\n";
    for (const auto& record : records) {
        out << "checkpoint " << record.checkpoint_id << " ts=" << record.timestamp_ms
            << " wal=" << record.wal_start << "-" << record.wal_end
            << " pages=" << record.dirty_pages_flushed
            << " mode=" << mode_text(record.mode)
            << " manifest=" << record.manifest_hash << "\n";
    }
    return out.str();
}

uint64_t CheckpointScheduler::last_checkpoint_time() const {
    if (records.empty()) return 0;
    return records.back().timestamp_ms;
}

size_t CheckpointScheduler::completed_count() const {
    return records.size();
}

bool CheckpointPolicyParser::parse_text(const std::string& text, CheckpointPolicy& out) const {
    std::istringstream input(text);
    std::string line;
    bool seen = false;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        try {
            if (key == "max_wal_bytes") out.max_wal_bytes = std::stoull(value);
            else if (key == "max_dirty_pages") out.max_dirty_pages = static_cast<uint32_t>(std::stoul(value));
            else if (key == "periodic_interval_ms") out.periodic_interval_ms = static_cast<uint32_t>(std::stoul(value));
            else if (key == "min_interval_ms") out.min_interval_ms = static_cast<uint32_t>(std::stoul(value));
            else if (key == "max_replica_lag") out.max_replica_lag = static_cast<uint32_t>(std::stoul(value));
            else if (key == "full_every_n") out.full_every_n = static_cast<uint32_t>(std::stoul(value));
            else if (key == "allow_during_backup") out.allow_during_backup = parse_bool(value);
            else if (key == "prefer_incremental") out.prefer_incremental = parse_bool(value);
            seen = true;
        } catch (...) {
            return false;
        }
    }
    return seen;
}

std::string CheckpointPolicyParser::emit_text(const CheckpointPolicy& policy) const {
    std::ostringstream out;
    out << "max_wal_bytes=" << policy.max_wal_bytes << "\n";
    out << "max_dirty_pages=" << policy.max_dirty_pages << "\n";
    out << "periodic_interval_ms=" << policy.periodic_interval_ms << "\n";
    out << "min_interval_ms=" << policy.min_interval_ms << "\n";
    out << "max_replica_lag=" << policy.max_replica_lag << "\n";
    out << "full_every_n=" << policy.full_every_n << "\n";
    out << "allow_during_backup=" << (policy.allow_during_backup ? "true" : "false") << "\n";
    out << "prefer_incremental=" << (policy.prefer_incremental ? "true" : "false") << "\n";
    return out.str();
}

} // namespace FenrirDB
