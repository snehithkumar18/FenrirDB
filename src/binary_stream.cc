#include "binary_stream.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <set>

namespace FenrirDB {

namespace {

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

    for (uint64_t i = 0; i < frame_count; ++i) {
        if (!parse_frame(data, size, offset, static_cast<size_t>(i))) {
            return false;
        }
    }
    return issue_list.empty();
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

    uint32_t combined_len = static_cast<uint32_t>(name_len + payload_len);
    if (offset + combined_len > size) {
        issue_list.push_back({frame_index, "frame body is truncated"});
        return false;
    }

    frame.name.assign(reinterpret_cast<const char*>(data + offset), static_cast<size_t>(name_len));
    offset += static_cast<size_t>(name_len);
    if (!StreamFrameCodec::unescape_payload(data + offset, static_cast<size_t>(payload_len), frame.payload)) {
        issue_list.push_back({frame_index, "payload escape sequence is invalid"});
        return false;
    }
    offset += static_cast<size_t>(payload_len);

    if (StreamFrameCodec::checksum(frame.payload) != frame.checksum) {
        issue_list.push_back({frame_index, "payload checksum does not match"});
    }
    frame_list.push_back(std::move(frame));
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
    uint32_t h = 2166136261u;
    for (uint8_t b : bytes) {
        h ^= b;
        h *= 16777619u;
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
            --estimate;
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
                payload[out++] = 0x7d;
                payload[out++] = 0x00;
                continue;
            }
            b = static_cast<uint8_t>(escaped ^ 0x20);
        }
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
