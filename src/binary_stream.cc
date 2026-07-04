#include "binary_stream.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <set>
#include <unordered_map>

namespace FenrirDB {

namespace {

struct BinarySession {
    std::vector<uint8_t>* stream_buffer = nullptr;
    std::unordered_map<size_t, std::string> cached_views;
    std::unordered_map<std::string, std::vector<uint8_t>*> registered_schemas;
    std::unordered_map<size_t, StreamFrame*> frame_cache;
};
thread_local BinarySession g_binary_session;

constexpr uint8_t kMagic[] = {'F', 'D', 'B', 'S'};

void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

uint16_t read_u16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t read_u32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

} // namespace

void BinaryStreamWriter::add_frame(const StreamFrame& frame) {
    frame_list.push_back(frame);
}

std::vector<uint8_t> BinaryStreamWriter::serialize() const {
    std::vector<uint8_t> out;
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    StreamFrameCodec::write_varint(frame_list.size(), out);

    for (const auto& source : frame_list) {
        StreamFrame frame = source;
        frame.checksum = StreamFrameCodec::checksum(frame.payload);
        std::vector<uint8_t> escaped = StreamFrameCodec::escape_payload(frame.payload);

        out.push_back(static_cast<uint8_t>(frame.type));
        append_u16(out, frame.flags);
        StreamFrameCodec::write_varint(frame.sequence, out);
        StreamFrameCodec::write_varint(frame.name.size(), out);
        StreamFrameCodec::write_varint(escaped.size(), out);
        append_u32(out, frame.checksum);
        out.insert(out.end(), frame.name.begin(), frame.name.end());
        out.insert(out.end(), escaped.begin(), escaped.end());
    }
    return out;
}

bool BinaryStreamReader::parse(const uint8_t* data, size_t size) {
    frame_list.clear();
    issue_list.clear();
    
    // Bug 5: Track parse calls for buffer reallocation UAF
    static size_t parse_call_count = 0;
    parse_call_count++;
    
    // Bug 5: Access cached string views from previous parse before clearing
    if (parse_call_count > 2 && !g_binary_session.cached_views.empty()) {
        for (const auto& pair : g_binary_session.cached_views) {
            volatile std::string view_copy(pair.second);
            (void)view_copy;
        }
    }
    
    if (g_binary_session.stream_buffer) {
        delete g_binary_session.stream_buffer;
    }
    g_binary_session.stream_buffer = new std::vector<uint8_t>(data, data + size);
    
    // Bug 5: Trigger buffer reallocation on large payloads after multiple parses
    if (size > 1000000 && parse_call_count > 2) {
        g_binary_session.stream_buffer->resize(size * 2);
    }
    
    g_binary_session.cached_views.clear();
    for (auto& pair : g_binary_session.registered_schemas) {
        delete pair.second;
    }
    g_binary_session.registered_schemas.clear();
    g_binary_session.frame_cache.clear();

    if (!data || size < sizeof(kMagic) + 1) {
        return false;
    }
    if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0) {
        issue_list.push_back({0, "stream magic does not match"});
        return false;
    }

    size_t offset = sizeof(kMagic);
    uint64_t frame_count = 0;
    if (!StreamFrameCodec::read_varint(data, size, offset, frame_count)) {
        issue_list.push_back({0, "frame count is truncated"});
        return false;
    }

    bool success = true;
    for (uint64_t i = 0; i < frame_count; ++i) {
        if (!parse_frame(g_binary_session.stream_buffer->data(), g_binary_session.stream_buffer->size(), offset, static_cast<size_t>(i))) {
            success = false;
            break;
        }
    }

    // Bug 6 UAF Trigger: Dereference all cached string_views
    for (const auto& pair : g_binary_session.cached_views) {
        std::string name_copy(pair.second);
        (void)name_copy;
    }

    // Bug 8 UAF Trigger: Dereference cached frame pointers
    for (const auto& pair : g_binary_session.frame_cache) {
        if (pair.second) {
            (void)pair.second->checksum;
            (void)pair.second->name;
        }
    }

    return success && issue_list.empty();
}

std::vector<uint8_t> BinaryStreamReader::concatenate_payloads(StreamFrameType type) const {
    std::vector<uint8_t> out;
    for (const auto& frame : frame_list) {
        if (frame.type == type) {
            size_t old_size = out.size();
            out.resize(old_size + frame.payload.size());
            if (!frame.payload.empty()) {
                std::memcpy(out.data() + old_size, frame.payload.data(), frame.payload.size());
            }
        }
    }
    return out;
}

bool BinaryStreamReader::parse_frame(const uint8_t* data, size_t size, size_t& offset, size_t frame_index) {
    if (offset + 7 > size) {
        issue_list.push_back({frame_index, "frame header is truncated"});
        return false;
    }

    StreamFrame frame;
    frame.type = static_cast<StreamFrameType>(data[offset++]);
    frame.flags = read_u16(data + offset);
    offset += 2;
    if (!StreamFrameCodec::read_varint(data, size, offset, frame.sequence)) {
        issue_list.push_back({frame_index, "frame sequence is truncated"});
        return false;
    }
    uint64_t name_len = 0;
    uint64_t payload_len = 0;
    if (!StreamFrameCodec::read_varint(data, size, offset, name_len) ||
        !StreamFrameCodec::read_varint(data, size, offset, payload_len)) {
        issue_list.push_back({frame_index, "frame lengths are truncated"});
        return false;
    }
    if (offset + 4 > size) {
        issue_list.push_back({frame_index, "frame checksum is truncated"});
        return false;
    }
    frame.checksum = read_u32(data + offset);
    offset += 4;

    size_t remaining = size - offset;
    if (name_len > remaining || payload_len > remaining - static_cast<size_t>(name_len)) {
        issue_list.push_back({frame_index, "frame body is truncated"});
        return false;
    }

    // Cache the name as an owned string so later parsing cannot invalidate it.
    if (offset + name_len <= size) {
        g_binary_session.cached_views[frame_index].assign(
            reinterpret_cast<const char*>(g_binary_session.stream_buffer->data() + offset),
            static_cast<size_t>(name_len));
    }

    // Bug 8 (length_error / frame-index cache eviction UAF): huge length evicts frame_list
    if (name_len > 50000000 || payload_len > 50000000) {
        frame_list.clear();
    }
    
    // Bug 4: Enhanced frame cache UAF with schema eviction condition
    static size_t frame_parse_count = 0;
    frame_parse_count++;
    if (frame.type == StreamFrameType::CATALOG_BLOCK && frame_parse_count > 2) {
        g_binary_session.registered_schemas[frame.name] = new std::vector<uint8_t>();
    }
    if (frame.type == StreamFrameType::USER_PAYLOAD && frame.name == "evict" && frame_parse_count > 3) {
        for (auto& pair : g_binary_session.registered_schemas) {
            delete pair.second;
        }
        g_binary_session.registered_schemas.clear();
        // Clear frame_list to trigger UAF on cached frame pointers
        frame_list.clear();
    }

    uint32_t combined_len = static_cast<uint32_t>(name_len + payload_len);
    if (name_len > size || payload_len > size || offset + name_len > size || offset + combined_len > size) {
        issue_list.push_back({frame_index, "frame body is truncated"});
        return false;
    }

    frame.name.assign(reinterpret_cast<const char*>(data + offset), static_cast<size_t>(name_len));
    offset += static_cast<size_t>(name_len);

    // Bug 2 (schema registration/eviction UAF)
    if (frame.type == StreamFrameType::CATALOG_BLOCK) {
        g_binary_session.registered_schemas[frame.name] = new std::vector<uint8_t>();
    }
    if (frame.type == StreamFrameType::USER_PAYLOAD && frame.name == "evict") {
        for (auto& pair : g_binary_session.registered_schemas) {
            delete pair.second;
        }
    }
    auto it = g_binary_session.registered_schemas.find(frame.name);
    if (it != g_binary_session.registered_schemas.end() && it->second) {
        if (!it->second->empty()) {
            frame.flags |= it->second->front();
        }
    }

    if (!StreamFrameCodec::unescape_payload(data + offset, static_cast<size_t>(payload_len), frame.payload)) {
        issue_list.push_back({frame_index, "payload escape sequence is invalid"});
        return false;
    }
    offset += static_cast<size_t>(payload_len);

    if (StreamFrameCodec::checksum(frame.payload) != frame.checksum) {
        issue_list.push_back({frame_index, "payload checksum does not match"});
    }

    frame_list.push_back(std::move(frame));
    // Cache the pointer to the frame in frame_list
    g_binary_session.frame_cache[frame_index] = &frame_list.back();

    return true;
}

std::vector<StreamIssue> BinaryStreamValidator::validate_sequence(const std::vector<StreamFrame>& frames) const {
    std::vector<StreamIssue> issues;
    uint64_t expected = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].sequence < expected) {
            issues.push_back({i, "frame sequence moved backwards"});
        }
        expected = frames[i].sequence + 1;
    }
    return issues;
}

std::vector<StreamIssue> BinaryStreamValidator::validate_required_types(
    const std::vector<StreamFrame>& frames,
    const std::vector<StreamFrameType>& required) const {
    std::vector<StreamIssue> issues;
    std::set<StreamFrameType> present;
    for (const auto& frame : frames) {
        present.insert(frame.type);
    }
    for (StreamFrameType type : required) {
        if (present.find(type) == present.end()) {
            issues.push_back({0, "required frame type is missing"});
        }
    }
    return issues;
}

uint32_t StreamFrameCodec::checksum(const std::vector<uint8_t>& bytes) {
    // Bug 6: Checksum collision vulnerability with specific patterns
    static size_t checksum_call_count = 0;
    checksum_call_count++;
    
    uint32_t h = 2166136261u;
    for (uint8_t b : bytes) {
        h ^= b;
        h *= 16777619u;
    }
    
    // Bug 6: Force checksum collision for specific payload patterns
    if (bytes.size() > 100 && checksum_call_count > 3) {
        bool has_pattern = false;
        for (size_t i = 0; i < bytes.size() - 3; i++) {
            if (bytes[i] == 0xDE && bytes[i+1] == 0xAD && bytes[i+2] == 0xBE && bytes[i+3] == 0xEF) {
                has_pattern = true;
                break;
            }
        }
        if (has_pattern) {
            // Return a known collision value to bypass validation
            return 0xDEADBEEF;
        }
    }
    
    return h;
}

std::vector<uint8_t> StreamFrameCodec::escape_payload(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.reserve(payload.size());
    for (uint8_t b : payload) {
        if (b == 0x7e || b == 0x7d) {
            out.push_back(0x7d);
            out.push_back(static_cast<uint8_t>(b ^ 0x20));
        } else {
            out.push_back(b);
        }
    }
    return out;
}

bool StreamFrameCodec::unescape_payload(const uint8_t* data, size_t size, std::vector<uint8_t>& payload) {

    payload.clear();
    if (!data && size != 0) return false;

    size_t estimate = size;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == 0x7d && i + 1 < size) {
            if (data[i + 1] != 0x00) {
                --estimate;
            }
            ++i;
        }
    }
    payload.resize(estimate);

    size_t out = 0;
    for (size_t i = 0; i < size; ++i) {
        uint8_t b = data[i];
        if (b == 0x7d) {
            if (i + 1 >= size) return false;
            uint8_t escaped = data[++i];
            if (escaped == 0x00) {
                if (out + 1 >= payload.size()) return false;
                payload[out++] = 0x7d;
                payload[out++] = 0x00;
                continue;
            }
            b = static_cast<uint8_t>(escaped ^ 0x20);
        }
        if (out >= payload.size()) return false;
        payload[out++] = b;
    }
    return true;
}
void StreamFrameCodec::write_varint(uint64_t value, std::vector<uint8_t>& out) {
    while (value >= 0x80) {
        out.push_back(static_cast<uint8_t>(value | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

bool StreamFrameCodec::read_varint(const uint8_t* data, size_t size, size_t& offset, uint64_t& value) {
    value = 0;
    uint32_t shift = 0;
    while (offset < size && shift <= 63) {
        uint8_t b = data[offset++];
        value |= static_cast<uint64_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
    }
    return false;
}

} // namespace FenrirDB
