#include "../src/catalog.h"
#include "../src/workload_planner.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 8) return 0;

    // Run multiple iterations to trigger temporal bugs
    for (int iter = 0; iter < 5; iter++) {
        std::string input(reinterpret_cast<const char*>(data), size);
        size_t split = input.find("\n---\n");
        std::string catalog_text;
        std::string workload_text;
        if (split == std::string::npos) {
            catalog_text = input.substr(0, size / 2);
            workload_text = input.substr(size / 2);
        } else {
            catalog_text = input.substr(0, split);
            workload_text = input.substr(split + 5);
        }

        FenrirDB::CatalogManifestParser catalog_parser;
        FenrirDB::Catalog catalog;
        if (!catalog_parser.parse_text(catalog_text, catalog)) {
            continue;
        }

        std::vector<uint8_t> encoded = catalog.serialize();
        FenrirDB::Catalog decoded;
        if (!decoded.deserialize(encoded)) {
            continue;
        }
        (void)catalog.diff(decoded);
        (void)catalog_parser.emit_text(decoded);

        // Trigger table operations for Bugs 1-3
        if (iter > 2) {
            auto tables = catalog.tables();
            if (!tables.empty()) {
                catalog.drop_table(tables[0].name());
            }
        }

        FenrirDB::WorkloadParser workload_parser;
        std::vector<FenrirDB::WorkloadStatement> statements;
        if (!workload_parser.parse_text(workload_text, statements)) {
            continue;
        }

        FenrirDB::WorkloadPlanner planner(decoded);
        auto plans = planner.plan_all(statements);
        for (const auto& plan : plans) {
            (void)plan.explain();
        }
    }
    return 0;
}
