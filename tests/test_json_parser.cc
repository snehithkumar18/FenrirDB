#include "../src/json_parser.h"
#include <iostream>
#include <cassert>

void test_json_lexer() {
    std::cout << "Running test_json_lexer..." << std::endl;
    std::string json = "{\"name\": \"Alice\", \"age\": 30, \"active\": true, \"roles\": [\"admin\", \"user\"]}";
    
    FenrirDB::JSONLexer lexer(json);
    std::vector<FenrirDB::JSONToken> tokens = lexer.tokenize();

    // Verify token sequence
    assert(!tokens.empty());
    assert(tokens[0].type == FenrirDB::JSONTokenType::CURLY_OPEN);
    assert(tokens[1].type == FenrirDB::JSONTokenType::STRING && tokens[1].text == "name");
    assert(tokens[2].type == FenrirDB::JSONTokenType::COLON);
    assert(tokens[3].type == FenrirDB::JSONTokenType::STRING && tokens[3].text == "Alice");
    assert(tokens[4].type == FenrirDB::JSONTokenType::COMMA);
    assert(tokens[5].type == FenrirDB::JSONTokenType::STRING && tokens[5].text == "age");
    assert(tokens[6].type == FenrirDB::JSONTokenType::COLON);
    assert(tokens[7].type == FenrirDB::JSONTokenType::NUMBER && tokens[7].text == "30");

    std::cout << "test_json_lexer passed." << std::endl;
}

void test_json_parser_basic() {
    std::cout << "Running test_json_parser_basic..." << std::endl;
    std::string json = "{\"name\": \"Alice\", \"age\": 30, \"active\": true}";
    
    FenrirDB::JSONLexer lexer(json);
    std::vector<FenrirDB::JSONToken> tokens = lexer.tokenize();

    FenrirDB::JSONParser parser(tokens);
    FenrirDB::Variant var = parser.parse();

    assert(var.type == FenrirDB::VariantType::MAP);
    std::unordered_map<std::string, FenrirDB::Variant> map = var.get_map();

    assert(map.find("name") != map.end() && map["name"].get_string() == "Alice");
    assert(map.find("age") != map.end() && map["age"].get_int() == 30);
    assert(map.find("active") != map.end() && map["active"].get_bool() == true);

    std::cout << "test_json_parser_basic passed." << std::endl;
}

void test_json_parser_nested() {
    std::cout << "Running test_json_parser_nested..." << std::endl;
    std::string json = "{\"user\": {\"name\": \"Bob\", \"age\": 25}, \"codes\": [10, 20]}";

    FenrirDB::JSONLexer lexer(json);
    std::vector<FenrirDB::JSONToken> tokens = lexer.tokenize();

    FenrirDB::JSONParser parser(tokens);
    FenrirDB::Variant var = parser.parse();

    assert(var.type == FenrirDB::VariantType::MAP);
    std::unordered_map<std::string, FenrirDB::Variant> map = var.get_map();

    assert(map.find("user") != map.end() && map["user"].type == FenrirDB::VariantType::MAP);
    std::unordered_map<std::string, FenrirDB::Variant> user = map["user"].get_map();
    assert(user["name"].get_string() == "Bob");
    assert(user["age"].get_int() == 25);

    assert(map.find("codes") != map.end() && map["codes"].type == FenrirDB::VariantType::ARRAY);
    std::vector<FenrirDB::Variant> codes = map["codes"].get_array();
    assert(codes.size() == 2);
    assert(codes[0].get_int() == 10);
    assert(codes[1].get_int() == 20);

    std::cout << "test_json_parser_nested passed." << std::endl;
}

void test_json_serializer() {
    std::cout << "Running test_json_serializer..." << std::endl;

    std::unordered_map<std::string, FenrirDB::Variant> inner;
    inner["nested_key"] = FenrirDB::Variant("nested_val");

    std::unordered_map<std::string, FenrirDB::Variant> obj;
    obj["name"] = FenrirDB::Variant("Alice");
    obj["age"] = FenrirDB::Variant(30);
    obj["sub"] = FenrirDB::Variant(inner);

    FenrirDB::Variant var(obj);
    std::string serialized = FenrirDB::JSONSerializer::serialize(var);

    // Parse it back to verify serialization correctness
    FenrirDB::JSONLexer lexer(serialized);
    std::vector<FenrirDB::JSONToken> tokens = lexer.tokenize();
    FenrirDB::JSONParser parser(tokens);
    FenrirDB::Variant parsed_var = parser.parse();

    assert(parsed_var.type == FenrirDB::VariantType::MAP);
    std::unordered_map<std::string, FenrirDB::Variant> parsed_map = parsed_var.get_map();
    assert(parsed_map["name"].get_string() == "Alice");
    assert(parsed_map["age"].get_int() == 30);
    assert(parsed_map["sub"].type == FenrirDB::VariantType::MAP);

    std::cout << "test_json_serializer passed." << std::endl;
}

int main() {
    test_json_lexer();
    test_json_parser_basic();
    test_json_parser_nested();
    test_json_serializer();
    std::cout << "All JSON Parser tests passed successfully!" << std::endl;
    return 0;
}
