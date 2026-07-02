#ifndef FENRIRDB_WORKLOAD_CODEC_H
#define FENRIRDB_WORKLOAD_CODEC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace FenrirDB {

enum class WorkloadOpType : uint8_t {
    MANIFEST = 1,
    EXPRESSION = 2,
    PIPELINE = 3,
    CACHE = 4,
    SERIALIZE = 5
};

struct WorkloadOp {
    WorkloadOpType type = WorkloadOpType::SERIALIZE;
    uint16_t flags = 0;
    uint32_t arg0 = 0;
    uint32_t arg1 = 0;
    std::string name;
    std::vector<uint8_t> payload;
};

class BinaryWorkload {
public:
    uint16_t version = 1;
    std::vector<WorkloadOp> ops;

    std::vector<uint8_t> serialize() const;
};

class BinaryWorkloadReader {
public:
    bool parse(const uint8_t* data, size_t size, BinaryWorkload& out) const;

private:
    static bool decode_payload(const uint8_t* data, size_t encoded_len,
                               std::vector<uint8_t>& out);
};

} // namespace FenrirDB

#endif // FENRIRDB_WORKLOAD_CODEC_H
