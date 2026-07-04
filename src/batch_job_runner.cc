#include "batch_job_runner.h"

#include <algorithm>
#include <cctype>
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

std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string status_text(BatchTaskStatus status) {
    if (status == BatchTaskStatus::READY) return "ready";
    if (status == BatchTaskStatus::RUNNING) return "running";
    if (status == BatchTaskStatus::SUCCEEDED) return "succeeded";
    if (status == BatchTaskStatus::FAILED) return "failed";
    if (status == BatchTaskStatus::SKIPPED) return "skipped";
    return "pending";
}

bool parse_uint32(const std::string& str, uint32_t& out_val) {
    if (str.empty()) return false;
    size_t processed = 0;
    try {
        unsigned long val = std::stoul(str, &processed);
        if (processed != str.size() || val > UINT32_MAX) return false;
        out_val = static_cast<uint32_t>(val);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

std::string BatchRunResult::report() const {
    std::ostringstream out;
    out << (success ? "success" : "failed") << " cost=" << total_cost << "\n";
    for (const auto& event : events) {
        out << event.timestamp_ms << " task=" << event.task_id
            << " status=" << status_text(event.status)
            << " " << event.message << "\n";
    }
    return out.str();
}

bool BatchJobParser::parse_text(const std::string& text, BatchJob& out) const {
    out = BatchJob();
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts = split(line, ' ');
        if (parts.empty()) continue;
        if (parts[0] == "job" && parts.size() >= 2) {
            out.id = parts[1];
        } else if (parts[0] == "task" && parts.size() >= 3) {
            BatchTask task;
            task.id = parts[1];
            task.type = parse_type(parts[2]);
            for (size_t i = 3; i < parts.size(); ++i) {
                size_t eq = parts[i].find('=');
                if (eq == std::string::npos) continue;
                std::string key = parts[i].substr(0, eq);
                std::string value = parts[i].substr(eq + 1);
                if (key == "deps") task.dependencies = split_csv(value);
                else if (key == "attempts") {
                    uint32_t val;
                    if (parse_uint32(value, val)) task.retry.max_attempts = val;
                }
                else if (key == "backoff") {
                    uint32_t val;
                    if (parse_uint32(value, val)) task.retry.backoff_ms = val;
                }
                else if (key == "cost") {
                    uint32_t val;
                    if (parse_uint32(value, val)) task.estimated_cost = val;
                }
                else task.params[key] = value;
            }
            out.tasks.push_back(task);
        }
    }
    return !out.id.empty() && !out.tasks.empty();
}

std::string BatchJobParser::emit_text(const BatchJob& job) const {
    std::ostringstream out;
    out << "job " << job.id << "\n";
    for (const auto& task : job.tasks) {
        out << "task " << task.id << " " << type_text(task.type);
        if (!task.dependencies.empty()) {
            out << " deps=";
            for (size_t i = 0; i < task.dependencies.size(); ++i) {
                if (i) out << ",";
                out << task.dependencies[i];
            }
        }
        out << " attempts=" << task.retry.max_attempts
            << " backoff=" << task.retry.backoff_ms
            << " cost=" << task.estimated_cost;
        for (const auto& pair : task.params) {
            out << " " << pair.first << "=" << pair.second;
        }
        out << "\n";
    }
    return out.str();
}

BatchTaskType BatchJobParser::parse_type(const std::string& text) {
    if (text == "validate_catalog") return BatchTaskType::VALIDATE_CATALOG;
    if (text == "run_checkpoint") return BatchTaskType::RUN_CHECKPOINT;
    if (text == "plan_backup") return BatchTaskType::PLAN_BACKUP;
    if (text == "emit_telemetry") return BatchTaskType::EMIT_TELEMETRY;
    if (text == "wait") return BatchTaskType::WAIT_FOR_DEPENDENCIES;
    return BatchTaskType::PLAN_WORKLOAD;
}

std::string BatchJobParser::type_text(BatchTaskType type) {
    if (type == BatchTaskType::VALIDATE_CATALOG) return "validate_catalog";
    if (type == BatchTaskType::RUN_CHECKPOINT) return "run_checkpoint";
    if (type == BatchTaskType::PLAN_BACKUP) return "plan_backup";
    if (type == BatchTaskType::EMIT_TELEMETRY) return "emit_telemetry";
    if (type == BatchTaskType::WAIT_FOR_DEPENDENCIES) return "wait";
    return "plan_workload";
}

std::vector<std::string> BatchJobParser::split_csv(const std::string& text) {
    return split(text, ',');
}

BatchJobRunner::BatchJobRunner(const Catalog& cat, const WorkloadPlanner& plan)
    : catalog(cat), planner(plan) {}

BatchRunResult BatchJobRunner::dry_run(const BatchJob& job) const {
    BatchRunResult result;
    std::vector<BatchEvent> order_events;
    auto tasks = ordered_tasks(job, order_events);
    result.events = order_events;
    for (const BatchTask* task : tasks) {
        std::string message;
        if (!validate_task_params(*task, message)) {
            result.events.push_back({task->id, BatchTaskStatus::FAILED, 0, message});
            result.final_status[task->id] = BatchTaskStatus::FAILED;
        } else {
            result.events.push_back({task->id, BatchTaskStatus::READY, 0, "dry-run accepted"});
            result.final_status[task->id] = BatchTaskStatus::READY;
            result.total_cost += task->estimated_cost;
        }
    }
    result.success = std::none_of(result.final_status.begin(), result.final_status.end(),
        [](const auto& pair) { return pair.second == BatchTaskStatus::FAILED; });
    return result;
}

BatchRunResult BatchJobRunner::run(const BatchJob& job, uint64_t start_ms) {
    BatchRunResult result;
    auto tasks = ordered_tasks(job, result.events);
    for (const BatchTask* task : tasks) {
        if (!dependencies_satisfied(*task, result.final_status)) {
            result.final_status[task->id] = BatchTaskStatus::SKIPPED;
            result.events.push_back({task->id, BatchTaskStatus::SKIPPED, start_ms, "dependency not satisfied"});
            continue;
        }
        bool task_success = false;
        for (uint32_t attempt = 0; attempt < task->retry.max_attempts && !task_success; ++attempt) {
            uint64_t now = start_ms + result.events.size() * 10 + attempt * task->retry.backoff_ms;
            result.events.push_back({task->id, BatchTaskStatus::RUNNING, now, "attempt " + std::to_string(attempt + 1)});
            task_success = execute_task(*task, result, now + 1);
        }
        result.final_status[task->id] = task_success ? BatchTaskStatus::SUCCEEDED : BatchTaskStatus::FAILED;
        result.events.push_back({task->id, result.final_status[task->id], start_ms + result.events.size() * 10,
                                 task_success ? "task completed" : "task failed"});
        if (task_success) result.total_cost += task->estimated_cost;
    }
    result.success = std::none_of(result.final_status.begin(), result.final_status.end(),
        [](const auto& pair) { return pair.second == BatchTaskStatus::FAILED || pair.second == BatchTaskStatus::SKIPPED; });
    return result;
}

void BatchJobRunner::set_checkpoint_policy(const CheckpointPolicy& policy) {
    checkpoint_policy = policy;
}

void BatchJobRunner::set_backup_catalog(const BackupCatalog& backups) {
    backup_catalog = backups;
}

TelemetryReport BatchJobRunner::telemetry_report(uint64_t slow_threshold_ms) const {
    return telemetry.snapshot(slow_threshold_ms);
}

std::vector<const BatchTask*> BatchJobRunner::ordered_tasks(const BatchJob& job, std::vector<BatchEvent>& events) const {
    std::vector<const BatchTask*> ordered;
    std::set<std::string> emitted;
    for (size_t pass = 0; pass < job.tasks.size(); ++pass) {
        bool progressed = false;
        for (const auto& task : job.tasks) {
            if (emitted.find(task.id) != emitted.end()) continue;
            bool deps_known = std::all_of(task.dependencies.begin(), task.dependencies.end(),
                [&](const std::string& dep) {
                    return std::any_of(job.tasks.begin(), job.tasks.end(),
                        [&](const BatchTask& candidate) { return candidate.id == dep; });
                });
            bool deps_emitted = std::all_of(task.dependencies.begin(), task.dependencies.end(),
                [&](const std::string& dep) { return emitted.find(dep) != emitted.end(); });
            if (!deps_known) {
                events.push_back({task.id, BatchTaskStatus::FAILED, 0, "dependency is unknown"});
                emitted.insert(task.id);
                progressed = true;
            } else if (deps_emitted) {
                ordered.push_back(&task);
                emitted.insert(task.id);
                progressed = true;
            }
        }
        if (!progressed) break;
    }
    for (const auto& task : job.tasks) {
        if (emitted.find(task.id) == emitted.end()) {
            events.push_back({task.id, BatchTaskStatus::FAILED, 0, "dependency cycle detected"});
        }
    }
    return ordered;
}

bool BatchJobRunner::dependencies_satisfied(const BatchTask& task,
                                            const std::map<std::string, BatchTaskStatus>& status) const {
    for (const auto& dep : task.dependencies) {
        auto it = status.find(dep);
        if (it == status.end() || it->second != BatchTaskStatus::SUCCEEDED) return false;
    }
    return true;
}

bool BatchJobRunner::execute_task(const BatchTask& task, BatchRunResult& result, uint64_t now_ms) {
    std::string message;
    if (!validate_task_params(task, message)) {
        result.events.push_back({task.id, BatchTaskStatus::FAILED, now_ms, message});
        return false;
    }

    uint64_t span = telemetry.start_span(task.id, 0, now_ms);
    bool ok = true;
    if (task.type == BatchTaskType::VALIDATE_CATALOG) {
        ok = !catalog.tables().empty();
    } else if (task.type == BatchTaskType::PLAN_WORKLOAD) {
        WorkloadParser parser;
        std::vector<WorkloadStatement> statements;
        ok = parser.parse_text(task.params.at("workload"), statements);
        if (ok) {
            auto plans = planner.plan_all(statements);
            ok = std::all_of(plans.begin(), plans.end(), [](const WorkloadPlan& plan) { return plan.accepted; });
        }
    } else if (task.type == BatchTaskType::RUN_CHECKPOINT) {
        CheckpointScheduler scheduler(checkpoint_policy);
        CheckpointSignal signal;
        signal.now_ms = now_ms;
        signal.wal_bytes_since_checkpoint = task.params.count("wal") ? std::stoull(task.params.at("wal")) : checkpoint_policy.max_wal_bytes;
        signal.dirty_pages = task.params.count("dirty") ? static_cast<uint32_t>(std::stoul(task.params.at("dirty"))) : checkpoint_policy.max_dirty_pages;
        auto decision = scheduler.evaluate(signal);
        ok = decision.should_checkpoint;
    } else if (task.type == BatchTaskType::PLAN_BACKUP) {
        ok = !backup_catalog.backups().empty() || task.params.find("backup_id") != task.params.end();
    } else if (task.type == BatchTaskType::EMIT_TELEMETRY) {
        telemetry.add_counter("batch.tasks", 1, {{"task", task.id}}, now_ms);
    }
    telemetry.finish_span(span, now_ms + task.estimated_cost, {{"status", ok ? "ok" : "failed"}});
    return ok;
}

bool BatchJobRunner::validate_task_params(const BatchTask& task, std::string& message) const {
    if (task.id.empty()) {
        message = "task id is empty";
        return false;
    }
    if (task.retry.max_attempts == 0) {
        message = "retry attempts must be positive";
        return false;
    }
    if (task.type == BatchTaskType::PLAN_WORKLOAD && task.params.find("workload") == task.params.end()) {
        message = "workload task requires workload parameter";
        return false;
    }
    if (task.type == BatchTaskType::PLAN_BACKUP && task.params.find("backup_id") == task.params.end() && backup_catalog.backups().empty()) {
        message = "backup task requires backup context";
        return false;
    }
    message = "ok";
    return true;
}

} // namespace FenrirDB

