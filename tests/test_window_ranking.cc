#include "../src/query_engine_compiler_window_ranking.h"
#include <iostream>
#include <cassert>
#include <vector>

void test_window_lag_lead() {
    std::cout << "Running test_window_lag_lead..." << std::endl;

    std::vector<FenrirDB::Document> docs;
    {
        FenrirDB::Document d1;
        d1.set_field("dept", FenrirDB::Variant("HR"));
        d1.set_field("val", FenrirDB::Variant(10));
        d1.set_field("id", FenrirDB::Variant(1));
        docs.push_back(d1);

        FenrirDB::Document d2;
        d2.set_field("dept", FenrirDB::Variant("HR"));
        d2.set_field("val", FenrirDB::Variant(20));
        d2.set_field("id", FenrirDB::Variant(2));
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
    FenrirDB::RankingSpec spec;
    spec.partition_col = "dept";
    spec.order_col = "id";
    spec.target_col = "val";
    spec.func = FenrirDB::RankingFuncType::LAG;
    spec.offset = 1;
    spec.default_val = FenrirDB::Variant(0);

    FenrirDB::RankingWindowExecutor window_exec(std::move(child), spec);
    window_exec.init();

    FenrirDB::Document doc;
    FenrirDB::RecordID rid;

    int match_count = 0;
    while (window_exec.next(doc, rid)) {
        FenrirDB::Variant val_val, rank_val;
        assert(doc.get_field("val", val_val));
        assert(doc.get_field("ranking_result", rank_val));

        if (val_val.get_int() == 10) {
            assert(rank_val.get_int() == 0); // LAG default
            match_count++;
        } else if (val_val.get_int() == 20) {
            assert(rank_val.get_int() == 10); // LAG previous
            match_count++;
        }
    }
    assert(match_count == 2);
    window_exec.close();

    std::cout << "test_window_lag_lead passed." << std::endl;
}

int main() {
    test_window_lag_lead();
    std::cout << "All Window Ranking tests passed successfully!" << std::endl;
    return 0;
}
