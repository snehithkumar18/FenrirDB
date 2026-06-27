#include "../src/query_planner.h"
#include "../src/sql_parser.h"
#include <iostream>
#include <cassert>
#include <cstdio>

void test_seq_scan_executor() {
    std::cout << "Running test_seq_scan_executor..." << std::endl;
    std::string db_file = "test_planner.db";
    std::remove(db_file.c_str());

    {
        FenrirDB::Database db;
        db.open(db_file);

        FenrirDB::Document doc1;
        doc1.set_field("name", FenrirDB::Variant("Alice"));
        doc1.set_field("age", FenrirDB::Variant(30));
        db.insert("user1", doc1);

        FenrirDB::Document doc2;
        doc2.set_field("name", FenrirDB::Variant("Bob"));
        doc2.set_field("age", FenrirDB::Variant(25));
        db.insert("user2", doc2);

        db.close();
    }

    {
        FenrirDB::DiskManager disk_mgr(db_file);
        FenrirDB::BufferPoolManager cache_mgr(10, disk_mgr);

        FenrirDB::SeqScanExecutor executor(disk_mgr, cache_mgr);
        executor.init();

        FenrirDB::Document doc;
        FenrirDB::RecordID rid;

        // Fetch first record
        assert(executor.next(doc, rid) == true);
        FenrirDB::Variant name_val;
        assert(doc.get_field("name", name_val));
        assert(name_val.get_string() == "Alice");

        // Fetch second record
        assert(executor.next(doc, rid) == true);
        assert(doc.get_field("name", name_val));
        assert(name_val.get_string() == "Bob");

        // No more records
        assert(executor.next(doc, rid) == false);
        executor.close();
    }

    std::remove(db_file.c_str());
    std::cout << "test_seq_scan_executor passed." << std::endl;
}

void test_query_planner_index_scan() {
    std::cout << "Running test_query_planner_index_scan..." << std::endl;
    std::string db_file = "test_planner_index.db";
    std::remove(db_file.c_str());

    FenrirDB::DiskManager disk_mgr(db_file);
    FenrirDB::BufferPoolManager cache_mgr(10, disk_mgr);

    // Form root index page
    uint32_t root_id = disk_mgr.allocate_page();
    FenrirDB::Page* root_page = cache_mgr.fetch_page(root_id);
    if (root_page) {
        FenrirDB::IndexNode* node = reinterpret_cast<FenrirDB::IndexNode*>(root_page->data + 8);
        new (node) FenrirDB::IndexNode();
        node->is_leaf = true;
        cache_mgr.flush_page(root_id);
    }

    FenrirDB::BPlusTreeIndex index(disk_mgr, cache_mgr, root_id);
    
    // Allocate a doc page and record it
    uint32_t doc_page = disk_mgr.allocate_page();
    FenrirDB::Page* page = cache_mgr.fetch_page(doc_page);
    new (page) FenrirDB::Page(doc_page);
    
    FenrirDB::Document doc;
    doc.set_field("id", FenrirDB::Variant("user100"));
    doc.set_field("name", FenrirDB::Variant("Bob"));
    page->insert_record(0, doc.serialize().data(), doc.serialize().size());
    cache_mgr.flush_page(doc_page);

    index.insert("user100", { doc_page, 0 });

    // Plan query: SELECT name FROM users WHERE id = 'user100';
    FenrirDB::SQLSelectStatement stmt;
    stmt.fields = { "name" };
    stmt.table = "users";
    stmt.where_field = "id";
    stmt.where_op = FenrirDB::QueryOp::EQ;
    stmt.where_value = FenrirDB::Variant("user100");

    FenrirDB::QueryPlanner planner(disk_mgr, cache_mgr, index);
    auto executor = planner.plan_query(stmt);
    assert(executor != nullptr);

    executor->init();
    FenrirDB::Document result_doc;
    FenrirDB::RecordID rid;

    assert(executor->next(result_doc, rid) == true);
    FenrirDB::Variant name_val;
    assert(result_doc.get_field("name", name_val));
    assert(name_val.get_string() == "Bob");

    assert(executor->next(result_doc, rid) == false);
    executor->close();

    std::remove(db_file.c_str());
    std::cout << "test_query_planner_index_scan passed." << std::endl;
}

int main() {
    test_seq_scan_executor();
    test_query_planner_index_scan();
    std::cout << "All Query Planner tests passed successfully!" << std::endl;
    return 0;
}
