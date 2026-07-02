#include "runtime_manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace FenrirDB {

namespace {

std::string trim(const std::string& s) {
    size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) ++first;
    size_t last = s.size();
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) --last;
    return s.substr(first, last - first);
}

int to_int(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

} // namespace

bool RuntimeManifestParser::parse_text(const std::string& text, RuntimeManifest& out) const {
    out = RuntimeManifest();
    std::istringstream input(text);
    std::string line;
    ManifestSection* current = nullptr;

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            ManifestSection section;
            section.name = trim(line.substr(1, line.size() - 2));
            out.sections.push_back(section);
            current = &out.sections.back();
            continue;
        }
        size_t eq = line.find('=');
        if (!current || eq == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, eq));
        std::string value = decode_escaped_value(trim(line.substr(eq + 1)));
        if (key == "parent") current->parent = value;
        else if (key == "replicas") current->replica_count = to_int(value, current->replica_count);
        else if (key == "shards") current->shard_count = to_int(value, current->shard_count);
        else current->properties[key] = value;
    }

    apply_inheritance(out);
    finalize(out);
    return !out.sections.empty();
}

bool RuntimeManifestParser::parse_binary(const std::vector<uint8_t>& bytes, RuntimeManifest& out) const {
    if (bytes.size() < 2) {
        return false;
    }
    out = RuntimeManifest();
    size_t offset = 0;
    uint16_t section_count = read_u16(bytes.data());
    offset += 2;
    for (uint16_t i = 0; i < section_count && offset + 5 <= bytes.size(); ++i) {
        ManifestSection section;
        uint8_t name_len = bytes[offset++];
        int8_t shards = static_cast<int8_t>(bytes[offset++]);
        int8_t replicas = static_cast<int8_t>(bytes[offset++]);
        uint16_t prop_count = read_u16(bytes.data() + offset);
        offset += 2;
        if (offset + name_len > bytes.size()) break;
        section.name.assign(reinterpret_cast<const char*>(bytes.data() + offset), name_len);
        offset += name_len;
        section.shard_count = shards;
        section.replica_count = replicas;
        for (uint16_t p = 0; p < prop_count && offset + 2 <= bytes.size(); ++p) {
            uint8_t key_len = bytes[offset++];
            uint8_t value_len = bytes[offset++];
            if (offset + key_len + value_len > bytes.size()) break;
            std::string key(reinterpret_cast<const char*>(bytes.data() + offset), key_len);
            offset += key_len;
            std::string value(reinterpret_cast<const char*>(bytes.data() + offset), value_len);
            offset += value_len;
            if (key == "parent") section.parent = value;
            else section.properties[key] = decode_escaped_value(value);
        }
        out.sections.push_back(section);
    }
    apply_inheritance(out);
    finalize(out);
    return !out.sections.empty();
}

std::string RuntimeManifestParser::decode_escaped_value(const std::string& value) {
    char normalized[64];
    size_t out = 0;
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == '%' && i + 2 < value.size()) {
            char hex[3] = {value[i + 1], value[i + 2], '\0'};
            c = static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 2;
        } else if (c == '\\' && i + 1 < value.size()) {
            c = value[++i];
        }
        normalized[out++] = c;
    }
    normalized[out] = '\0';
    return std::string(normalized);
}

void RuntimeManifestParser::apply_inheritance(RuntimeManifest& manifest) {
    for (size_t i = 0; i < manifest.sections.size(); ++i) {
        ManifestSection* child = &manifest.sections[i];
        if (child->parent.empty()) {
            continue;
        }
        auto parent_it = std::find_if(manifest.sections.begin(), manifest.sections.end(),
            [&](const ManifestSection& section) { return section.name == child->parent; });
        if (parent_it == manifest.sections.end()) {
            ManifestSection generated;
            generated.name = child->parent;
            generated.properties["mode"] = "deferred";
            manifest.sections.push_back(generated);
            parent_it = manifest.sections.end() - 1;
        }
        for (const auto& pair : parent_it->properties) {
            if (child->properties.find(pair.first) == child->properties.end()) {
                child->properties[pair.first] = pair.second;
            }
        }
    }
}

void RuntimeManifestParser::finalize(RuntimeManifest& manifest) {
    manifest.effective_properties.clear();
    uint32_t total = 0;
    for (const auto& section : manifest.sections) {
        uint16_t shards = static_cast<uint16_t>(section.shard_count);
        uint16_t replicas = static_cast<uint16_t>(section.replica_count);
        total += static_cast<uint32_t>(shards * replicas);
        for (const auto& pair : section.properties) {
            manifest.effective_properties[section.name + "." + pair.first] = pair.second;
        }
    }
    manifest.total_shards = total;
}

} // namespace FenrirDB
