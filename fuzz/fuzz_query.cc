#include "../src/database.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 10) return 0;

    std::string query_str(reinterpret_cast<const char*>(data), size);

    // Create a temporary database in memory / local file
    FenrirDB::Database db;
    if (db.open("fuzz_query.db") != FenrirDB::DBErrorCode::SUCCESS) {
        return 0;
    }

    // Populate database with mock documents containing mixed variant types
    FenrirDB::Document doc1;
    doc1.set_field("name", FenrirDB::Variant("Alice"));
    doc1.set_field("age", FenrirDB::Variant(30));
    doc1.set_field("active", FenrirDB::Variant(true));
    db.insert("user1", doc1);

    FenrirDB::Document doc2;
    doc2.set_field("name", FenrirDB::Variant(12345)); // Int instead of String (type confusion trigger)
    doc2.set_field("age", FenrirDB::Variant("thirty")); // String instead of Int (type confusion trigger)
    doc2.set_field("active", FenrirDB::Variant(false));
    db.insert("user2", doc2);

    std::vector<FenrirDB::Document> results;
    db.query(query_str, results);

    db.close();
    std::remove("fuzz_query.db");
    return 0;
}
