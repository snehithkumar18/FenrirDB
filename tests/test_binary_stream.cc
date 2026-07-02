#include "../src/binary_stream.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    FenrirDB::BinaryStreamWriter writer;

    FenrirDB::StreamFrame catalog;
    catalog.type = FenrirDB::StreamFrameType::CATALOG_BLOCK;
    catalog.sequence = 0;
    catalog.name = "catalog";
    catalog.payload.assign({'t', 'a', 'b', 'l', 'e'});
    writer.add_frame(catalog);

    FenrirDB::StreamFrame wal;
    wal.type = FenrirDB::StreamFrameType::WAL_DELTA;
    wal.sequence = 1;
    wal.name = "wal";
    wal.payload.assign({0x7e, 0x7d, 1, 2, 3});
    writer.add_frame(wal);

    std::vector<uint8_t> bytes = writer.serialize();
    FenrirDB::BinaryStreamReader reader;
    assert(reader.parse(bytes.data(), bytes.size()));
    assert(reader.frames().size() == 2);
    assert(reader.frames()[1].payload.size() == 5);

    FenrirDB::BinaryStreamValidator validator;
    auto sequence_issues = validator.validate_sequence(reader.frames());
    assert(sequence_issues.empty());
    auto type_issues = validator.validate_required_types(
        reader.frames(),
        {FenrirDB::StreamFrameType::CATALOG_BLOCK, FenrirDB::StreamFrameType::WAL_DELTA});
    assert(type_issues.empty());

    std::vector<uint8_t> wal_payload = reader.concatenate_payloads(FenrirDB::StreamFrameType::WAL_DELTA);
    assert(wal_payload.size() == 5);
    assert(wal_payload[0] == 0x7e);
    assert(wal_payload[1] == 0x7d);

    std::cout << "binary stream tests passed\n";
    return 0;
}
