#include "storage_compressor_lzw.h"
#include "logger.h"

namespace FenrirDB {

LZWCompressor::LZWCompressor() {
    reset_dictionary();
}

void LZWCompressor::reset_dictionary() {
    compress_dict.clear();
    decompress_dict.clear();

    for (uint16_t i = 0; i < 256; ++i) {
        std::string ch(1, static_cast<char>(i));
        compress_dict[ch] = i;
        decompress_dict[i] = ch;
    }
}

std::vector<uint16_t> LZWCompressor::compress(const std::string& input) {
    reset_dictionary();
    std::vector<uint16_t> compressed_codes;
    if (input.empty()) return compressed_codes;

    std::string current = "";
    uint16_t next_code = 256;

    for (char c : input) {
        std::string combined = current + c;
        if (compress_dict.find(combined) != compress_dict.end()) {
            current = combined;
        } else {
            compressed_codes.push_back(compress_dict[current]);
            
            // Add to dictionary if not full
            if (next_code < 4096) {
                compress_dict[combined] = next_code++;
            } else {
                // Dictionary full, reset
                reset_dictionary();
                next_code = 256;
            }
            current = std::string(1, c);
        }
    }
    
    if (!current.empty()) {
        compressed_codes.push_back(compress_dict[current]);
    }
    
    Logger::get_instance().info("LZW", "LZW compression complete. Compressed codes: " + std::to_string(compressed_codes.size()));
    return compressed_codes;
}

std::string LZWCompressor::decompress(const std::vector<uint16_t>& input) {
    reset_dictionary();
    std::string decompressed = "";
    if (input.empty()) return decompressed;

    uint16_t next_code = 256;
    uint16_t old_code = input[0];
    std::string s = decompress_dict[old_code];
    decompressed += s;
    std::string c = "";
    c += s[0];

    for (size_t i = 1; i < input.size(); ++i) {
        uint16_t n_code = input[i];
        std::string entry = "";
        
        if (decompress_dict.find(n_code) != decompress_dict.end()) {
            entry = decompress_dict[n_code];
        } else if (n_code == next_code) {
            entry = decompress_dict[old_code] + c;
        } else {
            Logger::get_instance().error("LZW", "Invalid LZW decompression code encountered.");
            return "";
        }

        decompressed += entry;
        c = "";
        c += entry[0];

        // Add prefix to dictionary
        if (next_code < 4096) {
            decompress_dict[next_code++] = decompress_dict[old_code] + c;
        } else {
            reset_dictionary();
            next_code = 256;
        }
        old_code = n_code;
    }
    
    Logger::get_instance().info("LZW", "LZW decompression complete. Result length: " + std::to_string(decompressed.length()));
    return decompressed;
}

} // namespace FenrirDB
