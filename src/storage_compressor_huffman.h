#ifndef FENRIRDB_STORAGE_COMPRESSOR_HUFFMAN_H
#define FENRIRDB_STORAGE_COMPRESSOR_HUFFMAN_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace FenrirDB {

struct FlatHuffmanNode {
    char ch;
    int16_t left_idx;
    int16_t right_idx;
};

struct HuffmanNode {
    char ch;
    int freq;
    std::shared_ptr<HuffmanNode> left;
    std::shared_ptr<HuffmanNode> right;

    HuffmanNode(char c, int f) : ch(c), freq(f), left(nullptr), right(nullptr) {}
};

class HuffmanCompressor {
private:
    std::unordered_map<char, std::string> codes;
    std::shared_ptr<HuffmanNode> root;

    void build_tree(const std::string& text);
    void generate_codes(const std::shared_ptr<HuffmanNode>& node, const std::string& code);

public:
    HuffmanCompressor() = default;
    ~HuffmanCompressor() = default;

    std::pair<std::vector<uint8_t>, size_t> compress(const std::string& input);
    std::string decompress(const std::vector<uint8_t>& compressed_bytes, size_t bit_length);
};

} // namespace FenrirDB

#endif // FENRIRDB_STORAGE_COMPRESSOR_HUFFMAN_H
