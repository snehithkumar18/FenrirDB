#ifndef FENRIRDB_BATCH_JOB_RUNNER_H
#define FENRIRDB_BATCH_JOB_RUNNER_H

#include "backup_restore.h"
#include "catalog.h"
#include "checkpoint_scheduler.h"
#include "telemetry.h"
#include "workload_planner.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace FenrirDB {

enum class BatchTaskType : uint8_t {
    VALIDATE_CATALOG = 1,
    PLAN_WORKLOAD = 2,
    RUN_CHECKPOINT = 3,
    PLAN_BACKUP = 4,
    EMIT_TELEMETRY = 5,
    WAIT_FOR_DEPENDENCIES = 6
};

enum class BatchTaskStatus : uint8_t {
    PENDING = 1,
    READY = 2,
    RUNNING = 3,
    SUCCEEDED = 4,
    FAILED = 5,
    SKIPPED = 6
};

struct RetryPolicy {
    uint32_t max_attempts = 1;
    uint32_t backoff_ms = 0;
};

struct BatchTask {
    std::string id;
    BatchTaskType type = BatchTaskType::PLAN_WORKLOAD;
    std::vector<std::string> dependencies;
    std::map<std::string, std::string> params;
    RetryPolicy retry;
    uint32_t estimated_cost = 1;
};

struct BatchJob {
    std::string id;
    std::vector<BatchTask> tasks;
};

struct BatchEvent {
    std::string task_id;
    BatchTaskStatus status = BatchTaskStatus::PENDING;
    uint64_t timestamp_ms = 0;
    std::string message;
};

struct BatchRunResult {
    bool success = false;
    std::vector<BatchEvent> events;
    std::map<std::string, BatchTaskStatus> final_status;
    uint32_t total_cost = 0;
    std::string report() const;
};

class BatchJobParser {
public:
    bool parse_text(const std::string& text, BatchJob& out) const;
    std::string emit_text(const BatchJob& job) const;

private:
    static BatchTaskType parse_type(const std::string& text);
    static std::string type_text(BatchTaskType type);
    static std::vector<std::string> split_csv(const std::string& text);
};

class BatchJobRunner {
public:
    BatchJobRunner(const Catalog& catalog, const WorkloadPlanner& planner);

    BatchRunResult dry_run(const BatchJob& job) const;
    BatchRunResult run(const BatchJob& job, uint64_t start_ms);

    void set_checkpoint_policy(const CheckpointPolicy& policy);
    void set_backup_catalog(const BackupCatalog& backups);
    TelemetryReport telemetry_report(uint64_t slow_threshold_ms) const;

private:
    const Catalog& catalog;
    const WorkloadPlanner& planner;
    CheckpointPolicy checkpoint_policy;
    BackupCatalog backup_catalog;
    TelemetryRegistry telemetry;

    std::vector<const BatchTask*> ordered_tasks(const BatchJob& job, std::vector<BatchEvent>& events) const;
    bool dependencies_satisfied(const BatchTask& task,
                                const std::map<std::string, BatchTaskStatus>& status) const;
    bool execute_task(const BatchTask& task, BatchRunResult& result, uint64_t now_ms);
    bool validate_task_params(const BatchTask& task, std::string& message) const;
};

} // namespace FenrirDB

#endif // FENRIRDB_BATCH_JOB_RUNNER_H
