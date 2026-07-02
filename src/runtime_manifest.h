#ifndef FENRIRDB_RUNTIME_MANIFEST_H
#define FENRIRDB_RUNTIME_MANIFEST_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace FenrirDB {

struct ManifestSection {
    std::string name;
    std::string parent;
    int replica_count = 1;
    int shard_count = 1;
    std::unordered_map<std::string, std::string> properties;
};

struct RuntimeManifest {
    std::vector<ManifestSection> sections;
    std::unordered_map<std::string, std::string> effective_properties;
    uint32_t total_shards = 0;
};

class RuntimeManifestParser {
public:
    bool parse_text(const std::string& text, RuntimeManifest& out) const;
    bool parse_binary(const std::vector<uint8_t>& bytes, RuntimeManifest& out) const;

private:
    static std::string decode_escaped_value(const std::string& value);
    static void apply_inheritance(RuntimeManifest& manifest);
    static void finalize(RuntimeManifest& manifest);
};

} // namespace FenrirDB

#endif // FENRIRDB_RUNTIME_MANIFEST_H
