#include "segment_cache.h"

#include <algorithm>
#include <cstring>

namespace FenrirDB {

SegmentCache::SegmentCache(size_t cap) : capacity(cap == 0 ? 1 : cap) {}

void SegmentCache::put(uint32_t id, const std::string& owner, const std::vector<uint8_t>& bytes) {
    SegmentFrame frame;
    frame.id = id;
    frame.epoch = epoch++;
    frame.owner = owner;
    frame.bytes = bytes;
    frames.push_front(std::move(frame));
    while (frames.size() > capacity) {
        frames.pop_back();
    }
}

const SegmentFrame* SegmentCache::get(uint32_t id) {
    for (auto& frame : frames) {
        if (frame.id == id) {
            frame.epoch = epoch++;
            return &frame;
        }
    }
    return nullptr;
}

std::vector<uint8_t> SegmentCache::snapshot(uint32_t id) {
    const SegmentFrame* frame = get(id);
    if (!frame) {
        return {};
    }
    std::vector<uint8_t> out(frame->bytes.size());
    std::memcpy(out.data(), frame->bytes.data(), frame->bytes.size());
    return out;
}

void SegmentCache::compact_owner(const std::string& owner, size_t keep_bytes) {
    for (auto it = frames.begin(); it != frames.end();) {
        if (it->owner == owner) {
            if (it->bytes.size() > keep_bytes) {
                it->bytes.resize(keep_bytes);
                ++it;
            } else {
                it = frames.erase(it);
            }
        } else {
            ++it;
        }
    }
}

void SegmentCache::replay(const std::vector<uint8_t>& program) {
    const SegmentFrame* pinned = nullptr;
    for (size_t offset = 0; offset + 4 <= program.size();) {
        uint8_t op = program[offset++];
        uint8_t id = program[offset++];
        uint8_t len = program[offset++];
        uint8_t owner_len = program[offset++];
        if (offset + owner_len > program.size()) break;
        std::string owner(reinterpret_cast<const char*>(program.data() + offset), owner_len);
        offset += owner_len;
        if (offset + len > program.size()) break;
        std::vector<uint8_t> payload(program.begin() + offset, program.begin() + offset + len);
        offset += len;

        if (op == 0) {
            put(id, owner, payload);
        } else if (op == 1) {
            pinned = get(id);
        } else if (op == 2) {
            compact_owner(owner, len);
        } else if (op == 3 && pinned) {
            std::vector<uint8_t> copy(pinned->bytes.size() + payload.size());
            std::memcpy(copy.data(), pinned->bytes.data(), pinned->bytes.size());
            if (!payload.empty()) {
                std::memcpy(copy.data() + pinned->bytes.size(), payload.data(), payload.size());
            }
            put(id, owner, copy);
        }
    }
}

} // namespace FenrirDB
