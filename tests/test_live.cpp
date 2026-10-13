// Live stream mode selection against real /api/config captures (tests/fixtures/{dev,prod}).

#include "epgstation/Api.h"
#include "epgstation/Live.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace epgstation;

namespace
{
std::string ReadFile(const std::string& path)
{
  // tests/fixtures/prod (the author's production server) stays private: not in the public repository.
  if (path.rfind("prod/", 0) == 0 && !std::filesystem::exists(std::string(FIXTURES_DIR) + "/prod"))
    SKIP("no production fixtures");
  std::ifstream in(std::string(FIXTURES_DIR) + "/" + path);
  REQUIRE(in.good());
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

class NoHttp : public IHttp
{
public:
  HttpResponse Get(const std::string&) override { return {}; }
  HttpResponse Delete(const std::string&) override { return {}; }
  HttpResponse Post(const std::string&, const std::string&) override { return {}; }
  HttpResponse Put(const std::string&, const std::string&) override { return {}; }
};
} // namespace

TEST_CASE("live: real configs pick the unconverted mode by default")
{
  const std::string set = GENERATE("dev", "prod");
  CAPTURE(set);
  const auto config = ParseLiveConfig(ReadFile(set + "/config.json"));
  CHECK(config.enabled);
  REQUIRE(config.m2ts.size() == 3);
  CHECK(LiveModeNames(config) == "720p, 480p, 無変換");

  CHECK(SelectLiveMode(config, "") == 2);
  CHECK(SelectLiveMode(config, "無変換") == 2);
  CHECK(SelectLiveMode(config, "720p") == 0);
  CHECK(SelectLiveMode(config, "480p") == 1);
  CHECK(SelectLiveMode(config, "1080p") == -1);
}

TEST_CASE("live: disabled or odd configs")
{
  CHECK(SelectLiveMode(ParseLiveConfig(R"({"isEnableTSLiveStream":false,
      "streamConfig":{"live":{"ts":{"m2ts":[{"name":"無変換","isUnconverted":true}]}}}})"),
                       "") == -1);
  CHECK(SelectLiveMode(ParseLiveConfig(R"({"isEnableTSLiveStream":true})"), "") == -1);
  // No unconverted mode configured: the first one is the default.
  CHECK(SelectLiveMode(ParseLiveConfig(R"({"isEnableTSLiveStream":true,
      "streamConfig":{"live":{"ts":{"m2ts":[{"name":"720p","isUnconverted":false}]}}}})"),
                       "") == 0);
  CHECK_THROWS(ParseLiveConfig("[]"));
  CHECK_THROWS(ParseLiveConfig(R"({"streamConfig":{"live":{"ts":{"m2ts":[{"x":1}]}}}})"));
}

TEST_CASE("live: stream URL")
{
  NoHttp http;
  Api api(http, "http://h:8888/api/");
  CHECK(api.LiveStreamUrl(3211240960ULL, 2) ==
        "http://h:8888/api/streams/live/3211240960/m2ts?mode=2");
}
