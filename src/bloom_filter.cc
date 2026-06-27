#include "bloom_filter.h"
#include "logger.h"
#include <cmath>
#include <functional>

namespace FenrirDB {

BloomFilter::BloomFilter(size_t expected_items, double false_positive_rate) {
    // Calculate optimal number of bits: m = -n * ln(p) / (ln(2)^2)
    double ln2 = std::log(2.0);
    double m = -(static_cast<double>(expected_items) * std::log(false_positive_rate)) / (ln2 * ln2);
    num_bits = static_cast<size_t>(m);
    if (num_bits == 0) num_bits = 64;

    // Calculate optimal number of hash functions: k = (m/n) * ln(2)
    double k = (static_cast<double>(num_bits) / expected_items) * ln2;
    num_hashes = static_cast<size_t>(k);
    if (num_hashes == 0) num_hashes = 1;

    bits.resize(num_bits, false);

    Logger::get_instance().info("BloomFilter", "Initialized with " + std::to_string(num_bits)
        + " bits, " + std::to_string(num_hashes) + " hash functions for "
        + std::to_string(expected_items) + " expected items.");
}

size_t BloomFilter::hash1(const std::string& key) const {
    // djb2 hash
    unsigned long hash = 5381;
    for (char c : key) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % num_bits;
}

size_t BloomFilter::hash2(const std::string& key) const {
    // FNV-1a hash
    unsigned long hash = 2166136261;
    for (char c : key) {
        hash ^= static_cast<unsigned long>(c);
        hash *= 16777619;
    }
    return hash % num_bits;
}

void BloomFilter::insert(const std::string& key) {
    size_t h1 = hash1(key);
    size_t h2 = hash2(key);

    for (size_t i = 0; i < num_hashes; ++i) {
        size_t combined = (h1 + i * h2) % num_bits;
        bits[combined] = true;
    }
}

bool BloomFilter::may_contain(const std::string& key) const {
    size_t h1 = hash1(key);
    size_t h2 = hash2(key);

    for (size_t i = 0; i < num_hashes; ++i) {
        size_t combined = (h1 + i * h2) % num_bits;
        if (!bits[combined]) {
            return false;
        }
    }
    return true;
}

void BloomFilter::clear() {
    std::fill(bits.begin(), bits.end(), false);
}

double BloomFilter::estimated_false_positive_rate() const {
    size_t set_bits = 0;
    for (bool b : bits) {
        if (b) set_bits++;
    }
    double fill_ratio = static_cast<double>(set_bits) / num_bits;
    return std::pow(fill_ratio, static_cast<double>(num_hashes));
}

} // namespace FenrirDB
