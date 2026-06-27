#include "storage_compressor.h"
#include "logger.h"
#include <sstream>
#include <cctype>

namespace FenrirDB {

StorageCompressor::StorageCompressor() {
    initialize_dictionary();
}

void StorageCompressor::initialize_dictionary() {
    // Top common SQL/JSON schemas keywords
    dictionary = {
        "id", "name", "dept", "salary", "Engineering", "Sales", "R&D", 
        "value", "manager_id", "revenue", "year", "HR", "IT", "employees"
    };

    for (size_t i = 0; i < dictionary.size(); ++i) {
        reverse_dictionary[dictionary[i]] = static_cast<uint8_t>(i | 0x80); // High bit indicates dictionary token
    }
}

std::string StorageCompressor::compress_rle(const std::string& input) const {
    if (input.empty()) return "";

    std::string compressed = "";
    size_t length = input.length();
    
    for (size_t i = 0; i < length; ++i) {
        size_t run_count = 1;
        while (i + 1 < length && input[i] == input[i + 1] && run_count < 255) {
            run_count++;
            i++;
        }
        if (run_count > 3) {
            // Compress only runs larger than 3 characters
            compressed += std::to_string(run_count) + "~" + input[i];
        } else {
            // Otherwise write uncompressed run
            for (size_t j = 0; j < run_count; ++j) {
                compressed += input[i];
            }
        }
    }
    return compressed;
}

std::string StorageCompressor::decompress_rle(const std::string& input) const {
    std::string decompressed = "";
    size_t length = input.length();

    for (size_t i = 0; i < length; ++i) {
        if (std::isdigit(input[i])) {
            // Parse run count
            std::string num_str = "";
            while (i < length && std::isdigit(input[i])) {
                num_str += input[i];
                i++;
            }
            if (i < length && input[i] == '~') {
                i++; // Skip delimiter
                if (i < length) {
                    int count = std::stoi(num_str);
                    char char_val = input[i];
                    decompressed.append(count, char_val);
                }
            } else {
                decompressed += num_str;
                if (i < length) decompressed += input[i];
            }
        } else {
            decompressed += input[i];
        }
    }
    return decompressed;
}

std::vector<uint8_t> StorageCompressor::compress_dictionary(const std::string& input) const {
    std::vector<uint8_t> compressed;
    std::string current_word = "";

    auto flush_word = [this, &compressed, &current_word]() {
        if (current_word.empty()) return;
        auto it = reverse_dictionary.find(current_word);
        if (it != reverse_dictionary.end()) {
            compressed.push_back(it->second);
        } else {
            for (char c : current_word) {
                compressed.push_back(static_cast<uint8_t>(c));
            }
        }
        current_word.clear();
    };

    for (char c : input) {
        if (std::isalnum(c) || c == '_') {
            current_word += c;
        } else {
            flush_word();
            compressed.push_back(static_cast<uint8_t>(c));
        }
    }
    flush_word();
    return compressed;
}

std::string StorageCompressor::decompress_dictionary(const std::vector<uint8_t>& input) const {
    std::string decompressed = "";
    for (uint8_t b : input) {
        if (b & 0x80) {
            size_t idx = b & 0x7F;
            if (idx < dictionary.size()) {
                decompressed += dictionary[idx];
            } else {
                decompressed += static_cast<char>(b);
            }
        } else {
            decompressed += static_cast<char>(b);
        }
    }
    return decompressed;
}

std::vector<uint8_t> StorageCompressor::compress_record(const std::string& record_str) const {
    std::string rle_comp = compress_rle(record_str);
    return compress_dictionary(rle_comp);
}

std::string StorageCompressor::decompress_record(const std::vector<uint8_t>& compressed_bytes) const {
    std::string dict_decomp = decompress_dictionary(compressed_bytes);
    return decompress_rle(dict_decomp);
}

} // namespace FenrirDB
