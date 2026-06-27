#ifndef FENRIRDB_BLOOM_FILTER_H
#define FENRIRDB_BLOOM_FILTER_H

#include <vector>
#include <string>
#include <cstdint>

namespace FenrirDB {

class BloomFilter {
private:
    std::vector<bool> bits;
    size_t num_bits;
    size_t num_hashes;

    size_t hash1(const std::string& key) const;
    size_t hash2(const std::string& key) const;

public:
    BloomFilter(size_t expected_items, double false_positive_rate = 0.01);
    ~BloomFilter() = default;

    void insert(const std::string& key);
    bool may_contain(const std::string& key) const;
    void clear();
    double estimated_false_positive_rate() const;
};

} // namespace FenrirDB

#endif // FENRIRDB_BLOOM_FILTER_H
