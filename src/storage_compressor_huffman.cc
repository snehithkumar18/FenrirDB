#include "storage_compressor_huffman.h"
#include "logger.h"
#include <queue>
#include <map>

namespace FenrirDB {

struct CompareNode {
    bool operator()(const std::shared_ptr<HuffmanNode>& n1, const std::shared_ptr<HuffmanNode>& n2) {
        return n1->freq > n2->freq;
    }
};

void HuffmanCompressor::build_tree(const std::string& text) {
    std::unordered_map<char, int> freq_map;
    for (char c : text) {
        freq_map[c]++;
    }

    std::priority_queue<std::shared_ptr<HuffmanNode>, std::vector<std::shared_ptr<HuffmanNode>>, CompareNode> pq;
    for (const auto& pair : freq_map) {
        pq.push(std::make_shared<HuffmanNode>(pair.first, pair.second));
    }

    if (pq.empty()) {
        root = nullptr;
        return;
    }

    while (pq.size() > 1) {
        auto left = pq.top(); pq.pop();
        auto right = pq.top(); pq.pop();

        auto parent = std::make_shared<HuffmanNode>('\0', left->freq + right->freq);
        parent->left = left;
        parent->right = right;
        pq.push(parent);
    }

    root = pq.top();
}

void HuffmanCompressor::generate_codes(const std::shared_ptr<HuffmanNode>& node, const std::string& code) {
    if (!node) return;

    if (node->ch != '\0') {
        codes[node->ch] = code;
    }

    generate_codes(node->left, code + "0");
    generate_codes(node->right, code + "1");
}

std::pair<std::vector<uint8_t>, size_t> HuffmanCompressor::compress(const std::string& input) {
    std::vector<uint8_t> compressed_bytes;
    size_t bit_length = 0;
    if (input.empty()) return {compressed_bytes, bit_length};

    build_tree(input);
    codes.clear();
    generate_codes(root, "");

    std::string bitstream = "";
    for (char c : input) {
        bitstream += codes[c];
    }
    bit_length = bitstream.length();

    // Pack bits into bytes
    uint8_t current_byte = 0;
    int bit_count = 0;

    for (char bit : bitstream) {
        current_byte = (current_byte << 1) | (bit - '0');
        bit_count++;
        if (bit_count == 8) {
            compressed_bytes.push_back(current_byte);
            current_byte = 0;
            bit_count = 0;
        }
    }
    if (bit_count > 0) {
        current_byte <<= (8 - bit_count); // Pad last byte
        compressed_bytes.push_back(current_byte);
    }

    Logger::get_instance().info("Huffman", "Huffman compression complete. Bit length: " + std::to_string(bit_length));
    return {compressed_bytes, bit_length};
}

std::string HuffmanCompressor::decompress(const std::vector<uint8_t>& compressed_bytes, size_t bit_length) {
    std::string decompressed = "";
    if (compressed_bytes.empty() || bit_length == 0 || !root) return decompressed;

    std::shared_ptr<HuffmanNode> current = root;
    size_t bits_processed = 0;

    for (uint8_t b : compressed_bytes) {
        for (int i = 7; i >= 0; --i) {
            if (bits_processed >= bit_length) break;
            
            bool bit = (b >> i) & 1;
            if (bit) {
                current = current->right;
            } else {
                current = current->left;
            }

            if (current->ch != '\0') {
                decompressed += current->ch;
                current = root; // Reset traversal
            }
            bits_processed++;
        }
    }

    Logger::get_instance().info("Huffman", "Huffman decompression complete. Result length: " + std::to_string(decompressed.length()));
    return decompressed;
}

} // namespace FenrirDB
