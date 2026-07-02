#include "../src/catalog.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    FenrirDB::CatalogManifestParser parser;
    FenrirDB::Catalog catalog;
    const std::string manifest =
        "table users\n"
        "version 3\n"
        "field id int required unique min=0\n"
        "field email string required maxlen=80\n"
        "field age int default=18 min=0 max=130\n"
        "field active bool default=true\n"
        "index users_pk btree id unique\n"
        "index users_email hash email sparse include=id,active\n";

    assert(parser.parse_text(manifest, catalog));
    assert(catalog.has_table("users"));

    const FenrirDB::TableSchema* users = catalog.get_table("users");
    assert(users != nullptr);
    assert(users->version() == 3);
    assert(users->has_field("email"));
    assert(users->has_index("users_pk"));

    FenrirDB::Document doc;
    doc.set_field("id", FenrirDB::Variant(7));
    doc.set_field("email", FenrirDB::Variant(std::string("a@example.test")));
    auto issues = catalog.validate_insert("users", doc);
    assert(issues.empty());

    FenrirDB::Document materialized = catalog.materialize_insert("users", doc);
    FenrirDB::Variant age;
    FenrirDB::Variant active;
    assert(materialized.get_field("age", age));
    assert(materialized.get_field("active", active));
    assert(age.get_int() == 18);
    assert(active.get_bool());

    FenrirDB::Document bad;
    bad.set_field("id", FenrirDB::Variant(-1));
    bad.set_field("email", FenrirDB::Variant(std::string("b@example.test")));
    auto bad_issues = catalog.validate_insert("users", bad);
    assert(!bad_issues.empty());

    auto bytes = catalog.serialize();
    FenrirDB::Catalog decoded;
    assert(decoded.deserialize(bytes));
    assert(decoded.describe() == catalog.describe());

    FenrirDB::Catalog next = decoded;
    FenrirDB::TableSchema orders("orders");
    orders.add_field(FenrirDB::FieldDefinition("id", FenrirDB::FieldKind::INT));
    assert(next.create_table(orders));
    FenrirDB::CatalogDiff diff = catalog.diff(next);
    assert(diff.added_tables.size() == 1);
    assert(diff.added_tables[0] == "orders");

    std::string emitted = parser.emit_text(catalog);
    FenrirDB::Catalog reparsed;
    assert(parser.parse_text(emitted, reparsed));
    assert(reparsed.has_table("users"));

    std::cout << "catalog tests passed\n";
    return 0;
}
