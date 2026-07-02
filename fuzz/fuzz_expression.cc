#include "../src/runtime_expression.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 4) return 0;

    std::string expr(reinterpret_cast<const char*>(data), size);
    FenrirDB::RuntimeExpressionParser parser;
    auto parsed = parser.parse(expr);
    if (!parsed) return 0;

    FenrirDB::RuntimeRow row;
    row.set("id", FenrirDB::Variant(static_cast<int>(size & 0xffff)));
    row.set("score", FenrirDB::Variant(static_cast<int>((size * 7) & 0xffff)));
    row.set("region", FenrirDB::Variant(std::string("west")));
    row.set("deep_alias", FenrirDB::Variant(42));
    row.set("slot.region", FenrirDB::Variant(std::string("east")));

    FenrirDB::RuntimeExpressionEngine engine;
    (void)engine.evaluate(*parsed, row);
    (void)engine.evaluate_bool(expr, row);
    return 0;
}
