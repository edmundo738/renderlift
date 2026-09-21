// RenderLift tests — minimal JSON parser.
#include "renderlift/core/Json.hpp"

#include "rl_test.hpp"

RL_TEST(json_parse_full_document) {
    const rl::json::Value doc = rl::json::parse(R"json(
        {
            "id": "gta5",
            "fps": 29.5,
            "uiNative": true,
            "ladder": [426, 480, 640],
            "reconstruction": { "mode": "edge", "sharpening": 0.35 },
            "missing": null
        }
    )json");

    RL_CHECK(doc.isObject());
    RL_CHECK(doc.at("id").asString() == "gta5");
    RL_CHECK_NEAR(doc.at("fps").asNumber(), 29.5, 1e-9);
    RL_CHECK(doc.at("uiNative").asBool());
    RL_CHECK(doc.at("ladder").asArray().size() == 3);
    RL_CHECK(doc.at("ladder").asArray()[2].asNumber() == 640.0);
    RL_CHECK(doc.at("reconstruction").find("mode")->asString() == "edge");
    RL_CHECK(doc.at("missing").isNull());
    RL_CHECK(doc.find("nope") == nullptr);
}

RL_TEST(json_escapes_and_numbers) {
    const rl::json::Value v = rl::json::parse(R"({"s": "a\"b\nc", "neg": -12.5e2, "uni": "Ã"})");
    RL_CHECK(v.at("s").asString() == "a\"b\nc");
    RL_CHECK_NEAR(v.at("neg").asNumber(), -1250.0, 1e-9);
    RL_CHECK(v.at("uni").asString().size() == 2);  // U+00C3 in UTF-8
}

RL_TEST(json_helpers_defaults) {
    const rl::json::Value v = rl::json::parse(R"({"a": 1})");
    RL_CHECK(v.stringOr("b", "fallback") == "fallback");
    RL_CHECK_NEAR(v.numberOr("b", 7.0), 7.0, 1e-9);
    RL_CHECK(v.boolOr("b", true));
    RL_CHECK_NEAR(v.numberOr("a", 0.0), 1.0, 1e-9);
}

RL_TEST(json_rejects_malformed) {
    RL_CHECK_THROWS(rl::json::parse("{"), rl::json::ParseError);
    RL_CHECK_THROWS(rl::json::parse(R"({"a" 1})"), rl::json::ParseError);
    RL_CHECK_THROWS(rl::json::parse("[1,2"), rl::json::ParseError);
    RL_CHECK_THROWS(rl::json::parse("{} trailing"), rl::json::ParseError);
    RL_CHECK_THROWS(rl::json::parse("nul"), rl::json::ParseError);
}

RL_TEST(json_type_errors) {
    const rl::json::Value v = rl::json::parse(R"({"a": "text"})");
    RL_CHECK_THROWS(v.at("a").asNumber(), std::runtime_error);
    RL_CHECK_THROWS(v.at("missing"), std::runtime_error);
}
