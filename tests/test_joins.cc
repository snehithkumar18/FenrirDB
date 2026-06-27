#include "../src/query_planner.h"
#include <iostream>
#include <cassert>
#include <vector>

void test_nested_loop_join() {
    std::cout << "Running test_nested_loop_join..." << std::endl;

    // Create mock outer dataset (users)
    std::vector<FenrirDB::Document> outer_docs;
    {
        FenrirDB::Document d1;
        d1.set_field("id", FenrirDB::Variant(1));
        d1.set_field("name", FenrirDB::Variant("Alice"));
        outer_docs.push_back(d1);

        FenrirDB::Document d2;
        d2.set_field("id", FenrirDB::Variant(2));
        d2.set_field("name", FenrirDB::Variant("Bob"));
        outer_docs.push_back(d2);
    }

    // Create mock inner dataset (logs)
    std::vector<FenrirDB::Document> inner_docs;
    {
        FenrirDB::Document d1;
        d1.set_field("user_id", FenrirDB::Variant(1));
        d1.set_field("event", FenrirDB::Variant("login"));
        inner_docs.push_back(d1);

        FenrirDB::Document d2;
        d2.set_field("user_id", FenrirDB::Variant(2));
        d2.set_field("event", FenrirDB::Variant("logout"));
        inner_docs.push_back(d2);
    }

    // Mock executors that read from in-memory vectors
    class MockVectorExecutor : public FenrirDB::AbstractExecutor {
    private:
        std::vector<FenrirDB::Document> docs;
        size_t cursor = 0;
    public:
        explicit MockVectorExecutor(const std::vector<FenrirDB::Document>& d) : docs(d) {}
        void init() override { cursor = 0; }
        bool next(FenrirDB::Document& doc, FenrirDB::RecordID& rid) override {
            if (cursor < docs.size()) {
                doc = docs[cursor++];
                rid = { 0, 0 };
                return true;
            }
            return false;
        }
        void close() override {}
    };

    auto out_exec = std::make_unique<MockVectorExecutor>(outer_docs);
    auto in_exec = std::make_unique<MockVectorExecutor>(inner_docs);

    FenrirDB::NestedLoopJoinExecutor join_exec(std::move(out_exec), std::move(in_exec), "id", "user_id");
    join_exec.init();

    FenrirDB::Document joined;
    FenrirDB::RecordID rid;

    // Verify row 1
    assert(join_exec.next(joined, rid) == true);
    FenrirDB::Variant name_val, event_val;
    assert(joined.get_field("name", name_val) && name_val.get_string() == "Alice");
    
    // Verify row 2
    assert(join_exec.next(joined, rid) == true);
    assert(joined.get_field("name", name_val) && name_val.get_string() == "Bob");

    assert(join_exec.next(joined, rid) == false);
    join_exec.close();

    std::cout << "test_nested_loop_join passed." << std::endl;
}

void test_hash_join() {
    std::cout << "Running test_hash_join..." << std::endl;

    std::vector<FenrirDB::Document> outer_docs;
    {
        FenrirDB::Document d1;
        d1.set_field("id", FenrirDB::Variant("A"));
        d1.set_field("val", FenrirDB::Variant(10));
        outer_docs.push_back(d1);
    }

    std::vector<FenrirDB::Document> inner_docs;
    {
        FenrirDB::Document d1;
        d1.set_field("key", FenrirDB::Variant("A"));
        d1.set_field("event", FenrirDB::Variant("hit"));
        inner_docs.push_back(d1);
    }

    class MockVectorExecutor : public FenrirDB::AbstractExecutor {
    private:
        std::vector<FenrirDB::Document> docs;
        size_t cursor = 0;
    public:
        explicit MockVectorExecutor(const std::vector<FenrirDB::Document>& d) : docs(d) {}
        void init() override { cursor = 0; }
        bool next(FenrirDB::Document& doc, FenrirDB::RecordID& rid) override {
            if (cursor < docs.size()) {
                doc = docs[cursor++];
                rid = { 0, 0 };
                return true;
            }
            return false;
        }
        void close() override {}
    };

    auto out_exec = std::make_unique<MockVectorExecutor>(outer_docs);
    auto in_exec = std::make_unique<MockVectorExecutor>(inner_docs);

    FenrirDB::HashJoinExecutor join_exec(std::move(out_exec), std::move(in_exec), "id", "key");
    join_exec.init();

    FenrirDB::Document joined;
    FenrirDB::RecordID rid;

    assert(join_exec.next(joined, rid) == true);
    FenrirDB::Variant val;
    assert(joined.get_field("val", val) && val.get_int() == 10);

    assert(join_exec.next(joined, rid) == false);
    join_exec.close();

    std::cout << "test_hash_join passed." << std::endl;
}

int main() {
    test_nested_loop_join();
    test_hash_join();
    std::cout << "All Joins tests passed successfully!" << std::endl;
    return 0;
}
