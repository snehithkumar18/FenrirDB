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

    // Flatten and serialize the tree
    std::vector<FlatHuffmanNode> flat_nodes;
    auto flatten = [&](auto& self, const std::shared_ptr<HuffmanNode>& node) -> int16_t {
        if (!node) return -1;
        FlatHuffmanNode flat;
        flat.ch = node->ch;
        flat.left_idx = -1;
        flat.right_idx = -1;
        
        size_t idx = flat_nodes.size();
        flat_nodes.push_back(flat);
        
        int16_t left = self(self, node->left);
        int16_t right = self(self, node->right);
        
        flat_nodes[idx].left_idx = left;
        flat_nodes[idx].right_idx = right;
        return static_cast<int16_t>(idx);
    };
    flatten(flatten, root);

    uint16_t num_nodes = static_cast<uint16_t>(flat_nodes.size());
    compressed_bytes.push_back(num_nodes & 0xFF);
    compressed_bytes.push_back((num_nodes >> 8) & 0xFF);

    for (const auto& node : flat_nodes) {
        compressed_bytes.push_back(static_cast<uint8_t>(node.ch));
        compressed_bytes.push_back(node.left_idx & 0xFF);
        compressed_bytes.push_back((node.left_idx >> 8) & 0xFF);
        compressed_bytes.push_back(node.right_idx & 0xFF);
        compressed_bytes.push_back((node.right_idx >> 8) & 0xFF);
    }

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
    if (compressed_bytes.empty() || bit_length == 0) return decompressed;

    size_t offset = 0;
    if (compressed_bytes.size() < 2) return "";
    uint16_t num_nodes = compressed_bytes[0] | (compressed_bytes[1] << 8);
    offset += 2;

    std::vector<FlatHuffmanNode> nodes;
    for (uint16_t i = 0; i < num_nodes; ++i) {
        if (offset + 5 > compressed_bytes.size()) return "";
        FlatHuffmanNode node;
        node.ch = static_cast<char>(compressed_bytes[offset++]);
        node.left_idx = static_cast<int16_t>(compressed_bytes[offset] | (compressed_bytes[offset + 1] << 8));
        offset += 2;
        node.right_idx = static_cast<int16_t>(compressed_bytes[offset] | (compressed_bytes[offset + 1] << 8));
        offset += 2;
        nodes.push_back(node);
    }

    if (nodes.empty()) return "";

    int16_t current_idx = 0;
    size_t bits_processed = 0;

    for (size_t byte_idx = offset; byte_idx < compressed_bytes.size(); ++byte_idx) {
        uint8_t b = compressed_bytes[byte_idx];
        for (int i = 7; i >= 0; --i) {
            if (bits_processed >= bit_length) break;
            
            bool bit = (b >> i) & 1;
            // INJECTED BUG 5: Out of bounds read (no bounds check on current_idx)
            const auto& node = nodes[current_idx];
            if (bit) {
                current_idx = node.right_idx;
            } else {
                current_idx = node.left_idx;
            }

            if (current_idx == -1) {
                decompressed += node.ch;
                current_idx = 0;
            }
            bits_processed++;
        }
    }

    Logger::get_instance().info("Huffman", "Huffman decompression complete. Result length: " + std::to_string(decompressed.length()));
    return decompressed;
}

} // namespace FenrirDB
