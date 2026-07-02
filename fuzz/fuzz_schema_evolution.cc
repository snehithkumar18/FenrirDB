#include "../src/catalog.h"
#include "../src/schema_evolution.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 16) return 0;

    std::string input(reinterpret_cast<const char*>(data), size);
    size_t split = input.find("\n---\n");
    if (split == std::string::npos) return 0;

    FenrirDB::CatalogManifestParser catalog_parser;
    FenrirDB::Catalog catalog;
    if (!catalog_parser.parse_text(input.substr(0, split), catalog)) {
        return 0;
    }

    FenrirDB::MigrationParser migration_parser;
    std::vector<FenrirDB::MigrationStep> steps;
    if (!migration_parser.parse_text(input.substr(split + 5), steps)) {
        return 0;
    }

    FenrirDB::Document doc;
    doc.set_field("id", FenrirDB::Variant(1));
    doc.set_field("email", FenrirDB::Variant(std::string("seed@example.test")));
    doc.set_field("score", FenrirDB::Variant(5));

    FenrirDB::SchemaMigrator migrator(catalog);
    FenrirDB::MigrationReport report = migrator.apply(steps, {doc});
    (void)migration_parser.emit_text(steps);
    (void)report.after.serialize();
    return 0;
}
