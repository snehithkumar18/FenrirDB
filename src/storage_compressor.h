#ifndef FENRIRDB_STORAGE_COMPRESSOR_H
#define FENRIRDB_STORAGE_COMPRESSOR_H

#include <string>
#include <vector>
#include <unordered_map>

namespace FenrirDB {

class StorageCompressor {
private:
    std::vector<std::string> dictionary;
    std::unordered_map<std::string, uint8_t> reverse_dictionary;

    void initialize_dictionary();

public:
    StorageCompressor();
    ~StorageCompressor() = default;

    // Run-Length Encoding (RLE)
    std::string compress_rle(const std::string& input) const;
    std::string decompress_rle(const std::string& input) const;

    // Dictionary Compression
    std::vector<uint8_t> compress_dictionary(const std::string& input) const;
    std::string decompress_dictionary(const std::vector<uint8_t>& input) const;

    // Combined compression pipeline
    std::vector<uint8_t> compress_record(const std::string& record_str) const;
    std::string decompress_record(const std::vector<uint8_t>& compressed_bytes) const;
};

} // namespace FenrirDB

#endif // FENRIRDB_STORAGE_COMPRESSOR_H
