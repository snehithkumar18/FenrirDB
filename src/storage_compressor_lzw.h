#ifndef FENRIRDB_STORAGE_COMPRESSOR_LZW_H
#define FENRIRDB_STORAGE_COMPRESSOR_LZW_H

#include <string>
#include <vector>
#include <unordered_map>

namespace FenrirDB {

class LZWCompressor {
private:
    std::unordered_map<std::string, uint16_t> compress_dict;
    std::unordered_map<uint16_t, std::string> decompress_dict;

    void reset_dictionary();

public:
    LZWCompressor();
    ~LZWCompressor() = default;

    std::vector<uint16_t> compress(const std::string& input);
    std::string decompress(const std::vector<uint16_t>& input);
};

} // namespace FenrirDB

#endif // FENRIRDB_STORAGE_COMPRESSOR_LZW_H
