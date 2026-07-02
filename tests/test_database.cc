#include "../src/database.h"
#include "../src/errors.h"
#include "../src/wal.h"
#include <iostream>
#include <cassert>
#include <cstdio>

using FenrirDB::DBErrorCode;

void test_database_basic() {
    std::cout << "Running test_database_basic..." << std::endl;
    std::string db_file = "test_db.db";
    std::remove(db_file.c_str());

    {
        FenrirDB::Database db;
        DBErrorCode res = db.open(db_file);
        assert(res == DBErrorCode::SUCCESS);

        FenrirDB::Document doc;
        doc.set_field("name", FenrirDB::Variant("Alice"));
        doc.set_field("age", FenrirDB::Variant(30));
        doc.set_field("active", FenrirDB::Variant(true));

        res = db.insert("user001", doc);
        assert(res == DBErrorCode::SUCCESS);
        db.close();
    }

    // Reopen and retrieve
    {
        FenrirDB::Database db;
        DBErrorCode res = db.open(db_file);
        assert(res == DBErrorCode::SUCCESS);

        FenrirDB::Document fetched;
        res = db.get("user001", fetched);
        assert(res == DBErrorCode::SUCCESS);

        FenrirDB::Variant val;
        assert(fetched.get_field("name", val));
        assert(val.get_string() == "Alice");

        assert(fetched.get_field("age", val));
        assert(val.get_int() == 30);

        assert(fetched.get_field("active", val));
        assert(val.get_bool() == true);

        db.close();
    }

    std::remove(db_file.c_str());
    std::cout << "test_database_basic passed." << std::endl;
}

void test_database_query() {
    std::cout << "Running test_database_query..." << std::endl;
    std::string db_file = "test_db_query.db";
    std::remove(db_file.c_str());

    FenrirDB::Database db;
    db.open(db_file);

    FenrirDB::Document doc1;
    doc1.set_field("name", FenrirDB::Variant("Bob"));
    doc1.set_field("age", FenrirDB::Variant(25));
    db.insert("user1", doc1);

    FenrirDB::Document doc2;
    doc2.set_field("name", FenrirDB::Variant("Charlie"));
    doc2.set_field("age", FenrirDB::Variant(45));
    db.insert("user2", doc2);

    std::vector<FenrirDB::Document> results;
    DBErrorCode res = db.query("age > 30", results);
    assert(res == DBErrorCode::SUCCESS);
    assert(results.size() == 1);

    FenrirDB::Variant val;
    results[0].get_field("name", val);
    assert(val.get_string() == "Charlie");

    db.close();
    std::remove(db_file.c_str());
    std::cout << "test_database_query passed." << std::endl;
}

int main() {
    test_database_basic();
    test_database_query();
    std::cout << "All Database tests passed successfully!" << std::endl;
    return 0;
}
