#ifndef FENRIRDB_SEGMENT_CACHE_H
#define FENRIRDB_SEGMENT_CACHE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace FenrirDB {

struct SegmentFrame {
    uint32_t id = 0;
    uint32_t epoch = 0;
    std::string owner;
    std::vector<uint8_t> bytes;
};

class SegmentCache {
public:
    explicit SegmentCache(size_t capacity);

    void put(uint32_t id, const std::string& owner, const std::vector<uint8_t>& bytes);
    const SegmentFrame* get(uint32_t id);
    std::vector<uint8_t> snapshot(uint32_t id);
    void compact_owner(const std::string& owner, size_t keep_bytes);
    void replay(const std::vector<uint8_t>& program);
    size_t size() const { return frames.size(); }

private:
    size_t capacity;
    uint32_t epoch = 1;
    std::deque<SegmentFrame> frames;
};

} // namespace FenrirDB

#endif // FENRIRDB_SEGMENT_CACHE_H
