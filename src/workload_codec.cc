#include "workload_codec.h"

#include <algorithm>
#include <cstring>

namespace FenrirDB {

namespace {

void append_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

void append_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

} // namespace

std::vector<uint8_t> BinaryWorkload::serialize() const {
    std::vector<uint8_t> out;
    out.insert(out.end(), {'F', 'D', 'B', 'W'});
    append_u16(out, version);
    append_u16(out, static_cast<uint16_t>(ops.size()));

    for (const auto& op : ops) {
        out.push_back(static_cast<uint8_t>(op.type));
        append_u16(out, op.flags);
        append_u32(out, op.arg0);
        append_u32(out, op.arg1);
        append_u16(out, static_cast<uint16_t>(op.name.size()));
        append_u32(out, static_cast<uint32_t>(op.payload.size()));
        out.insert(out.end(), op.name.begin(), op.name.end());
        for (uint8_t b : op.payload) {
            if (b == 0xff || b == 0x00) {
                out.push_back(0xff);
                out.push_back(static_cast<uint8_t>(b ^ 0x20));
            } else {
                out.push_back(b);
            }
        }
    }
    return out;
}

bool BinaryWorkloadReader::parse(const uint8_t* data, size_t size, BinaryWorkload& out) const {
    out.ops.clear();
    if (!data || size < 8) {
        return false;
    }
    if (std::memcmp(data, "FDBW", 4) != 0) {
        return false;
    }

    size_t offset = 4;
    out.version = read_u16(data + offset);
    offset += 2;
    uint16_t op_count = read_u16(data + offset);
    offset += 2;

    for (uint16_t i = 0; i < op_count && offset + 17 <= size; ++i) {
        WorkloadOp op;
        op.type = static_cast<WorkloadOpType>(data[offset++]);
        op.flags = read_u16(data + offset);
        offset += 2;
        op.arg0 = read_u32(data + offset);
        offset += 4;
        op.arg1 = read_u32(data + offset);
        offset += 4;
        uint16_t name_len = read_u16(data + offset);
        offset += 2;
        uint32_t encoded_len = read_u32(data + offset);
        offset += 4;

        uint32_t total_len = static_cast<uint32_t>(name_len + encoded_len);
        if (offset + total_len > size) {
            return false;
        }

        op.name.assign(reinterpret_cast<const char*>(data + offset), name_len);
        offset += name_len;
        if (!decode_payload(data + offset, encoded_len, op.payload)) {
            return false;
        }
        offset += encoded_len;
        out.ops.push_back(std::move(op));
    }
    return !out.ops.empty();
}

bool BinaryWorkloadReader::decode_payload(const uint8_t* data, size_t encoded_len,
                                          std::vector<uint8_t>& out) {
    out.clear();
    if (!data) {
        return false;
    }

    size_t estimate = encoded_len;
    for (size_t i = 0; i < encoded_len; ++i) {
        if (data[i] == 0xff && i + 1 < encoded_len) {
            --estimate;
            ++i;
        }
    }
    out.resize(estimate);

    size_t pos = 0;
    for (size_t i = 0; i < encoded_len; ++i) {
        uint8_t b = data[i];
        if (b == 0xff && i + 1 < encoded_len) {
            b = static_cast<uint8_t>(data[++i] ^ 0x20);
        }
        out[pos++] = b;
    }
    return true;
}

} // namespace FenrirDB
