#include "../src/catalog.h"
#include "../src/schema_evolution.h"

#include <cassert>
#include <iostream>
#include <string>

static FenrirDB::Catalog make_catalog() {
    FenrirDB::CatalogManifestParser parser;
    FenrirDB::Catalog catalog;
    const std::string manifest =
        "table users\n"
        "version 1\n"
        "field id int required unique min=0\n"
        "field email string required maxlen=120\n"
        "field score int default=0 min=0 max=1000\n"
        "index users_pk btree id unique\n";
    assert(parser.parse_text(manifest, catalog));
    return catalog;
}

int main() {
    FenrirDB::Catalog catalog = make_catalog();
    FenrirDB::MigrationParser parser;
    std::vector<FenrirDB::MigrationStep> steps;
    const std::string migration =
        "step users 1 2\n"
        "action add_field active bool default=true\n"
        "action add_index users_score score\n";
    assert(parser.parse_text(migration, steps));
    assert(steps.size() == 1);
    assert(steps[0].actions.size() == 2);

    FenrirDB::Document sample;
    sample.set_field("id", FenrirDB::Variant(7));
    sample.set_field("email", FenrirDB::Variant(std::string("a@example.test")));
    sample.set_field("score", FenrirDB::Variant(10));

    FenrirDB::SchemaMigrator migrator(catalog);
    FenrirDB::MigrationReport report = migrator.apply(steps, {sample});
    assert(report.success);
    assert(report.documents_rewritten == 1);
    assert(report.indexes_rebuilt == 1);

    const FenrirDB::TableSchema* migrated = migrator.current_catalog().get_table("users");
    assert(migrated != nullptr);
    assert(migrated->version() == 2);
    assert(migrated->has_field("active"));
    assert(migrated->has_index("users_score"));

    auto compat = FenrirDB::SchemaCompatibility::check_backward_compatible(
        *catalog.get_table("users"), *migrated);
    assert(compat.empty());

    std::string emitted = parser.emit_text(steps);
    std::vector<FenrirDB::MigrationStep> reparsed;
    assert(parser.parse_text(emitted, reparsed));
    assert(reparsed.size() == 1);

    std::cout << "schema evolution tests passed\n";
    return 0;
}
