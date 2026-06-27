#include "../src/query_planner.h"
#include <iostream>
#include <cassert>
#include <vector>

void test_sorting_executor() {
    std::cout << "Running test_sorting_executor..." << std::endl;

    std::vector<FenrirDB::Document> docs;
    {
        FenrirDB::Document d1;
        d1.set_field("val", FenrirDB::Variant(30));
        docs.push_back(d1);

        FenrirDB::Document d2;
        d2.set_field("val", FenrirDB::Variant(10));
        docs.push_back(d2);

        FenrirDB::Document d3;
        d3.set_field("val", FenrirDB::Variant(20));
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
    FenrirDB::SortExecutor sort_exec(std::move(child), "val", true); // Ascending
    sort_exec.init();

    FenrirDB::Document doc;
    FenrirDB::RecordID rid;

    assert(sort_exec.next(doc, rid) == true);
    FenrirDB::Variant val;
    assert(doc.get_field("val", val) && val.get_int() == 10);

    assert(sort_exec.next(doc, rid) == true);
    assert(doc.get_field("val", val) && val.get_int() == 20);

    assert(sort_exec.next(doc, rid) == true);
    assert(doc.get_field("val", val) && val.get_int() == 30);

    assert(sort_exec.next(doc, rid) == false);
    sort_exec.close();

    std::cout << "test_sorting_executor passed." << std::endl;
}

void test_aggregation_executor() {
    std::cout << "Running test_aggregation_executor..." << std::endl;

    std::vector<FenrirDB::Document> docs;
    {
        FenrirDB::Document d1;
        d1.set_field("dept", FenrirDB::Variant("HR"));
        d1.set_field("salary", FenrirDB::Variant(5000));
        docs.push_back(d1);

        FenrirDB::Document d2;
        d2.set_field("dept", FenrirDB::Variant("IT"));
        d2.set_field("salary", FenrirDB::Variant(8000));
        docs.push_back(d2);

        FenrirDB::Document d3;
        d3.set_field("dept", FenrirDB::Variant("HR"));
        d3.set_field("salary", FenrirDB::Variant(6000));
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
    FenrirDB::AggregationExecutor agg_exec(std::move(child), "salary", FenrirDB::AggType::SUM, "dept");
    agg_exec.init();

    FenrirDB::Document result;
    FenrirDB::RecordID rid;

    int match_count = 0;
    while (agg_exec.next(result, rid)) {
        FenrirDB::Variant dept_val, res_val;
        assert(result.get_field("dept", dept_val));
        assert(result.get_field("result", res_val));

        if (dept_val.get_string() == "HR") {
            assert(res_val.get_int() == 11000); // 5000 + 6000
            match_count++;
        } else if (dept_val.get_string() == "IT") {
            assert(res_val.get_int() == 8000);
            match_count++;
        }
    }
    assert(match_count == 2);
    agg_exec.close();

    std::cout << "test_aggregation_executor passed." << std::endl;
}

int main() {
    test_sorting_executor();
    test_aggregation_executor();
    std::cout << "All Aggregations tests passed successfully!" << std::endl;
    return 0;
}
