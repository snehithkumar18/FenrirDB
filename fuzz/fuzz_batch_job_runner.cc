#include "../src/batch_job_runner.h"
#include "../src/catalog.h"
#include "../src/workload_planner.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 16) return 0;

    std::string input(reinterpret_cast<const char*>(data), size);
    size_t split = input.find("\n---\n");
    if (split == std::string::npos) return 0;

    FenrirDB::CatalogManifestParser catalog_parser;
    FenrirDB::Catalog catalog;
    if (!catalog_parser.parse_text(input.substr(0, split), catalog)) return 0;

    FenrirDB::BatchJobParser job_parser;
    FenrirDB::BatchJob job;
    if (!job_parser.parse_text(input.substr(split + 5), job)) return 0;

    FenrirDB::WorkloadPlanner planner(catalog);
    FenrirDB::BatchJobRunner runner(catalog, planner);
    auto dry = runner.dry_run(job);
    (void)dry.report();
    auto result = runner.run(job, size * 10);
    (void)result.report();
    (void)job_parser.emit_text(job);
    (void)runner.telemetry_report(1).summary();
    return 0;
}
