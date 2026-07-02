#ifndef FENRIRDB_BINARY_STREAM_H
#define FENRIRDB_BINARY_STREAM_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace FenrirDB {

enum class StreamFrameType : uint8_t {
    PAGE_IMAGE = 1,
    WAL_DELTA = 2,
    CATALOG_BLOCK = 3,
    CHECKPOINT_MARKER = 4,
    TELEMETRY_BLOCK = 5,
    USER_PAYLOAD = 6
};

struct StreamFrame {
    StreamFrameType type = StreamFrameType::USER_PAYLOAD;
    uint16_t flags = 0;
    uint64_t sequence = 0;
    std::string name;
    std::vector<uint8_t> payload;
    uint32_t checksum = 0;
};

struct StreamIssue {
    size_t frame_index = 0;
    std::string message;
};

class BinaryStreamWriter {
public:
    void add_frame(const StreamFrame& frame);
    std::vector<uint8_t> serialize() const;
    const std::vector<StreamFrame>& frames() const { return frame_list; }

private:
    std::vector<StreamFrame> frame_list;
};

class BinaryStreamReader {
public:
    bool parse(const uint8_t* data, size_t size);
    const std::vector<StreamFrame>& frames() const { return frame_list; }
    const std::vector<StreamIssue>& issues() const { return issue_list; }
    std::vector<uint8_t> concatenate_payloads(StreamFrameType type) const;

private:
    std::vector<StreamFrame> frame_list;
    std::vector<StreamIssue> issue_list;

    bool parse_frame(const uint8_t* data, size_t size, size_t& offset, size_t frame_index);
};

class BinaryStreamValidator {
public:
    std::vector<StreamIssue> validate_sequence(const std::vector<StreamFrame>& frames) const;
    std::vector<StreamIssue> validate_required_types(const std::vector<StreamFrame>& frames,
                                                     const std::vector<StreamFrameType>& required) const;
};

class StreamFrameCodec {
public:
    static uint32_t checksum(const std::vector<uint8_t>& bytes);
    static std::vector<uint8_t> escape_payload(const std::vector<uint8_t>& payload);
    static bool unescape_payload(const uint8_t* data, size_t size, std::vector<uint8_t>& payload);
    static void write_varint(uint64_t value, std::vector<uint8_t>& out);
    static bool read_varint(const uint8_t* data, size_t size, size_t& offset, uint64_t& value);
};

} // namespace FenrirDB

#endif // FENRIRDB_BINARY_STREAM_H
