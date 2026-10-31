#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>

#include "mlagents/side_channels.h"

using namespace dy::mlagents;

namespace {

std::string blob(const std::string& name) {
    std::ifstream in("tests/data/sidechannel/" + name + ".bin", std::ios::binary);
    REQUIRE(in);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

nlohmann::json expected() {
    std::ifstream in("tests/data/sidechannel/expected.json");
    return nlohmann::json::parse(in);
}

void checkEnvParam(const ChannelMessage& m, const nlohmann::json& e) {
    CHECK(m.channel == envParamsChannel());
    EnvParamMessage p;
    REQUIRE(readEnvParam(m.payload, p));
    CHECK(p.key == e["key"].get<std::string>());
    if(e["type"] == "float") {
        CHECK(p.type == EnvParamMessage::Type::Float);
        CHECK(p.value == e["value"].get<float>());
    } else {
        CHECK(p.type == EnvParamMessage::Type::Sampler);
        CHECK(p.seed == e["seed"].get<int>());
        auto sampler = e["sampler"].get<std::string>();
        CHECK(p.sampler == (sampler == "uniform" ? EnvParamMessage::Sampler::Uniform : sampler == "gaussian" ? EnvParamMessage::Sampler::Gaussian
            : EnvParamMessage::Sampler::MultiRangeUniform));
        CHECK(p.args == e["args"].get<std::vector<float>>());
    }
}

void checkEngine(const ChannelMessage& m, const nlohmann::json& e) {
    CHECK(m.channel == engineConfigChannel());
    EngineConfigMessage c;
    REQUIRE(readEngineConfig(m.payload, c));
    CHECK((int) c.type == e["type"].get<int>());
    CHECK(c.values == e["values"].get<std::vector<float>>());
}

}

TEST_SUITE("protocol") {

TEST_CASE("P3.2 side channels decode the blobs captured from Python and re-encode them byte for byte") {

    auto exp = expected();

    for(const char* name : { "env_float", "env_uniform", "env_gaussian", "env_multirange" }) {
        CAPTURE(name);
        std::string data = blob(name);
        std::vector<ChannelMessage> messages;
        REQUIRE(readChannelMessages(data, messages));
        REQUIRE(messages.size() == exp[name].size());
        for(size_t i = 0; i < messages.size(); i++) checkEnvParam(messages[i], exp[name][i]);
        CHECK(writeChannelMessages(messages) == data);
    }

    {
        std::string data = blob("engine_config");
        std::vector<ChannelMessage> messages;
        REQUIRE(readChannelMessages(data, messages));
        REQUIRE(messages.size() == exp["engine_config"].size());
        for(size_t i = 0; i < messages.size(); i++) checkEngine(messages[i], exp["engine_config"][i]);
        CHECK(writeChannelMessages(messages) == data);
    }

    {
        std::string data = blob("mixed");
        std::vector<ChannelMessage> messages;
        REQUIRE(readChannelMessages(data, messages));
        REQUIRE(messages.size() == 2);
        checkEngine(messages[0], exp["mixed"][0]);
        checkEnvParam(messages[1], exp["mixed"][1]);
    }

    // What the engine writes for the stats channel equals what Python builds for it
    {
        std::vector<ChannelMessage> stats;
        for(auto& s : exp["stats"]) {
            stats.push_back({ statsChannel(), writeStat(s["key"].get<std::string>(), s["value"].get<float>(), (StatAggregation) s["aggregation"].get<int>()) });
        }
        CHECK(writeChannelMessages(stats) == blob("stats"));
    }

}

TEST_CASE("Side channels refuse truncated payloads") {
    std::string data = blob("env_float");
    std::vector<ChannelMessage> messages;
    CHECK(!readChannelMessages(data.substr(0, data.size() - 1), messages));
    EnvParamMessage p;
    CHECK(!readEnvParam(std::string("\x05\x00\x00\x00ab", 6), p));
}

}
