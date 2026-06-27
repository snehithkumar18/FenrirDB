#include "../src/query_planner.h"
#include <iostream>
#include <cassert>
#include <vector>

void test_projection_executor() {
    std::cout << "Running test_projection_executor..." << std::endl;

    std::vector<FenrirDB::Document> docs;
    {
        FenrirDB::Document d1;
        d1.set_field("name", FenrirDB::Variant("Alice"));
        d1.set_field("age", FenrirDB::Variant(30));
        d1.set_field("salary", FenrirDB::Variant(5000));
        docs.push_back(d1);
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

    auto child = std::make_unique<MockVectorExecutor>(docs);
    FenrirDB::ProjectionExecutor proj_exec(std::move(child), {"name", "salary"});
    proj_exec.init();

    FenrirDB::Document doc;
    FenrirDB::RecordID rid;

    assert(proj_exec.next(doc, rid) == true);
    FenrirDB::Variant val;
    assert(doc.get_field("name", val) && val.get_string() == "Alice");
    assert(doc.get_field("salary", val) && val.get_int() == 5000);
    assert(!doc.get_field("age", val)); // Project out

    assert(proj_exec.next(doc, rid) == false);
    proj_exec.close();

    std::cout << "test_projection_executor passed." << std::endl;
}

void test_having_executor() {
    std::cout << "Running test_having_executor..." << std::endl;

    std::vector<FenrirDB::Document> docs;
    {
        FenrirDB::Document d1;
        d1.set_field("result", FenrirDB::Variant(15));
        docs.push_back(d1);

        FenrirDB::Document d2;
        d2.set_field("result", FenrirDB::Variant(5));
        docs.push_back(d2);
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

    auto child = std::make_unique<MockVectorExecutor>(docs);
    // HAVING result > 10
    FenrirDB::HavingExecutor having_exec(std::move(child), "result", FenrirDB::QueryOp::GT, FenrirDB::Variant(10));
    having_exec.init();

    FenrirDB::Document doc;
    FenrirDB::RecordID rid;

    assert(having_exec.next(doc, rid) == true);
    FenrirDB::Variant val;
    assert(doc.get_field("result", val) && val.get_int() == 15);

    assert(having_exec.next(doc, rid) == false);
    having_exec.close();

    std::cout << "test_having_executor passed." << std::endl;
}

void test_distinct_executor() {
    std::cout << "Running test_distinct_executor..." << std::endl;

    std::vector<FenrirDB::Document> docs;
    {
        FenrirDB::Document d1;
        d1.set_field("name", FenrirDB::Variant("Alice"));
        docs.push_back(d1);

        FenrirDB::Document d2;
        d2.set_field("name", FenrirDB::Variant("Bob"));
        docs.push_back(d2);

        FenrirDB::Document d3;
        d3.set_field("name", FenrirDB::Variant("Alice")); // Duplicate
        docs.push_back(d3);
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

    auto child = std::make_unique<MockVectorExecutor>(docs);
    FenrirDB::DistinctExecutor distinct_exec(std::move(child), {"name"});
    distinct_exec.init();

    FenrirDB::Document doc;
    FenrirDB::RecordID rid;

    assert(distinct_exec.next(doc, rid) == true);
    FenrirDB::Variant val;
    assert(doc.get_field("name", val) && val.get_string() == "Alice");

    assert(distinct_exec.next(doc, rid) == true);
    assert(doc.get_field("name", val) && val.get_string() == "Bob");

    assert(distinct_exec.next(doc, rid) == false);
    distinct_exec.close();

    std::cout << "test_distinct_executor passed." << std::endl;
}

int main() {
    test_projection_executor();
    test_having_executor();
    test_distinct_executor();
    std::cout << "All Advanced Query Executors tests passed successfully!" << std::endl;
    return 0;
}
