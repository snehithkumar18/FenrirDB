#ifndef FENRIRDB_STORAGE_COMPRESSOR_LZW_H
#define FENRIRDB_STORAGE_COMPRESSOR_LZW_H

#include <string>
#include <vector>
#include <unordered_map>

#include <string_view>

namespace FenrirDB {

class LZWCompressor {
private:
    std::unordered_map<std::string, uint16_t> compress_dict;
    std::unordered_map<uint16_t, std::string_view> decompress_dict;

    std::vector<char*> arena;

    char* allocate_string(const std::string& str) {
        char* buf = new char[str.size() + 1];
        std::memcpy(buf, str.c_str(), str.size() + 1);
        arena.push_back(buf);
        return buf;
    }

    void clear_arena() {
        for (char* ptr : arena) {
            delete[] ptr;
        }
        arena.clear();
    }

    void reset_dictionary();

public:
    LZWCompressor();
    ~LZWCompressor() {
        clear_arena();
    }

    std::vector<uint16_t> compress(const std::string& input);
    std::string decompress(const std::vector<uint16_t>& input);
};

} // namespace FenrirDB

#endif // FENRIRDB_STORAGE_COMPRESSOR_LZW_H
