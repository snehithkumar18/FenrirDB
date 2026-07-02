#include "../src/batch_job_runner.h"
#include "../src/catalog.h"
#include "../src/workload_planner.h"

#include <cassert>
#include <iostream>
#include <string>

static FenrirDB::Catalog make_catalog() {
    FenrirDB::CatalogManifestParser parser;
    FenrirDB::Catalog catalog;
    assert(parser.parse_text(
        "table users\n"
        "version 1\n"
        "field id int required unique min=0\n"
        "field email string required maxlen=120\n"
        "field score int default=0 min=0 max=1000\n"
        "index users_pk btree id unique\n",
        catalog));
    return catalog;
}

int main() {
    FenrirDB::Catalog catalog = make_catalog();
    FenrirDB::WorkloadPlanner planner(catalog);
    FenrirDB::BatchJobParser parser;
    FenrirDB::BatchJob job;
    assert(parser.parse_text(
        "job nightly\n"
        "task validate validate_catalog cost=1\n"
        "task plan plan_workload deps=validate cost=3 workload=find|users|where=id=7|project=id,email\n"
        "task checkpoint run_checkpoint deps=plan cost=2 wal=2048 dirty=64\n"
        "task metrics emit_telemetry deps=checkpoint cost=1\n",
        job));

    FenrirDB::BatchJobRunner runner(catalog, planner);
    FenrirDB::CheckpointPolicy policy;
    policy.max_wal_bytes = 1024;
    policy.max_dirty_pages = 32;
    runner.set_checkpoint_policy(policy);

    FenrirDB::BatchRunResult dry = runner.dry_run(job);
    assert(dry.success);
    assert(dry.total_cost == 7);

    FenrirDB::BatchRunResult result = runner.run(job, 1000);
    assert(result.success);
    assert(result.final_status["metrics"] == FenrirDB::BatchTaskStatus::SUCCEEDED);
    assert(result.report().find("success") != std::string::npos);

    std::string emitted = parser.emit_text(job);
    FenrirDB::BatchJob reparsed;
    assert(parser.parse_text(emitted, reparsed));
    assert(reparsed.tasks.size() == job.tasks.size());

    auto telemetry = runner.telemetry_report(1);
    assert(!telemetry.slow_spans.empty());

    std::cout << "batch job runner tests passed\n";
    return 0;
}
