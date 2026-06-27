#include "../src/json_parser.h"
#include <iostream>
#include <cassert>
#include <sstream>

void test_extreme_nesting_depth(int depth) {
    std::cout << "Running test_extreme_nesting_depth with depth " << depth << "..." << std::endl;

    // Generate nested JSON string: {"val":{"val":...{"val":42}...}}
    std::string json;
    for (int i = 0; i < depth; ++i) {
        json += "{\"val\":";
    }
    json += "42";
    for (int i = 0; i < depth; ++i) {
        json += "}";
    }

    FenrirDB::JSONLexer lexer(json);
    std::vector<FenrirDB::JSONToken> tokens = lexer.tokenize();
    assert(!tokens.empty());

    FenrirDB::JSONParser parser(tokens);
    FenrirDB::Variant var = parser.parse();

    // Traverse variant down to the leaf to verify the value
    FenrirDB::Variant current = var;
    for (int i = 0; i < depth; ++i) {
        assert(current.type == FenrirDB::VariantType::MAP);
        std::unordered_map<std::string, FenrirDB::Variant> map = current.get_map();
        assert(map.find("val") != map.end());
        current = map["val"];
    }
    assert(current.type == FenrirDB::VariantType::INT);
    assert(current.get_int() == 42);

    std::cout << "test_extreme_nesting_depth passed." << std::endl;
}

void test_unicode_and_escapes() {
    std::cout << "Running test_unicode_and_escapes..." << std::endl;

    std::string json = "{\"unicode\": \"\\\\u0041\", \"escaped\": \"Line1\\\\nLine2\\\\tTabbed\", \"quotes\": \"\\\\\"Alice\\\\\"\"}";
    
    FenrirDB::JSONLexer lexer(json);
    std::vector<FenrirDB::JSONToken> tokens = lexer.tokenize();
    assert(!tokens.empty());

    FenrirDB::JSONParser parser(tokens);
    FenrirDB::Variant var = parser.parse();

    assert(var.type == FenrirDB::VariantType::MAP);
    std::unordered_map<std::string, FenrirDB::Variant> map = var.get_map();

    assert(map.find("unicode") != map.end() && map["unicode"].get_string() == "\\u0041");
    
    std::string esc_str = map["escaped"].get_string();
    assert(esc_str.find('\n') == std::string::npos); // raw escaped backslash characters are read literally in lexer
    
    std::cout << "test_unicode_and_escapes passed." << std::endl;
}

void test_large_array_nesting() {
    std::cout << "Running test_large_array_nesting..." << std::endl;

    // Generate JSON array containing 1000 items: [0, 1, 2, ..., 999]
    std::string json = "[";
    for (int i = 0; i < 1000; ++i) {
        json += std::to_string(i);
        if (i < 999) json += ",";
    }
    json += "]";

    FenrirDB::JSONLexer lexer(json);
    std::vector<FenrirDB::JSONToken> tokens = lexer.tokenize();
    assert(!tokens.empty());

    FenrirDB::JSONParser parser(tokens);
    FenrirDB::Variant var = parser.parse();

    assert(var.type == FenrirDB::VariantType::ARRAY);
    std::vector<FenrirDB::Variant> arr = var.get_array();
    assert(arr.size() == 1000);
    for (int i = 0; i < 1000; ++i) {
        assert(arr[i].type == FenrirDB::VariantType::INT);
        assert(arr[i].get_int() == i);
    }

    std::cout << "test_large_array_nesting passed." << std::endl;
}

int main() {
    test_extreme_nesting_depth(10);
    test_extreme_nesting_depth(50);
    test_unicode_and_escapes();
    test_large_array_nesting();
    std::cout << "All JSON Nesting stress tests passed successfully!" << std::endl;
    return 0;
}
