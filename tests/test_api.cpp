// Api over a scripted IHttp: URLs, error handling and the SQLITE_BUSY retry.

#include "epgstation/Api.h"

#include <deque>

#include <catch2/catch_test_macros.hpp>

using namespace epgstation;

namespace
{
class FakeHttp : public IHttp
{
public:
  std::deque<HttpResponse> responses;
  std::vector<std::string> urls; // "DELETE " prefix for deletes

  HttpResponse Get(const std::string& url) override { return Next(url); }
  HttpResponse Delete(const std::string& url) override { return Next("DELETE " + url); }
  HttpResponse Post(const std::string& url, const std::string& body) override
  {
    bodies.push_back(body);
    return Next("POST " + url);
  }
  HttpResponse Put(const std::string& url, const std::string& body) override
  {
    bodies.push_back(body);
    return Next("PUT " + url);
  }
  std::vector<std::string> bodies;

  HttpResponse Next(const std::string& url)
  {
    urls.push_back(url);
    REQUIRE_FALSE(responses.empty());
    HttpResponse r = responses.front();
    responses.pop_front();
    return r;
  }
};

const HttpResponse kBusy{500, R"({"code":500,"message":"Internal Server Error","errors":"SQLITE_BUSY: database is locked"})"};
} // namespace

TEST_CASE("api: base URL normalisation")
{
  CHECK(NormalizeBaseUrl("http://h:8888") == "http://h:8888");
  CHECK(NormalizeBaseUrl("http://h:8888/") == "http://h:8888");
  CHECK(NormalizeBaseUrl("http://h:8888/api") == "http://h:8888");
  CHECK(NormalizeBaseUrl("http://h:8888/api/") == "http://h:8888");
  CHECK(NormalizeBaseUrl("http://h/epgs/") == "http://h/epgs");
}

TEST_CASE("api: base64 for POST bodies")
{
  CHECK(Base64Encode("") == "");
  CHECK(Base64Encode("f") == "Zg==");
  CHECK(Base64Encode("fo") == "Zm8=");
  CHECK(Base64Encode("foo") == "Zm9v");
  CHECK(Base64Encode("foob") == "Zm9vYg==");
  CHECK(Base64Encode("手動") == "5omL5YuV"); // UTF-8 bytes
}

TEST_CASE("api: version and logo URL")
{
  FakeHttp http;
  http.responses.push_back({200, R"({"version":"2.10.0"})"});
  Api api(http, "http://h:8888/");
  CHECK(api.Version() == "2.10.0");
  CHECK(http.urls.at(0) == "http://h:8888/api/version");
  CHECK(api.ChannelLogoUrl(3211240960ULL) == "http://h:8888/api/channels/3211240960/logo");
}

TEST_CASE("api: SQLITE_BUSY is retried, then reported")
{
  FakeHttp http;
  Api api(http, "http://h:8888");
  int sleeps = 0;
  api.sleep = [&](int) { ++sleeps; };

  SECTION("recovers within the retry budget")
  {
    http.responses = {kBusy, kBusy, {200, "[]"}};
    CHECK(api.Channels().empty());
    CHECK(sleeps == 2);
  }
  SECTION("gives up after kRetries")
  {
    for (int i = 0; i <= Api::kRetries; ++i)
      http.responses.push_back(kBusy);
    try
    {
      api.Channels();
      FAIL("expected ApiError");
    }
    catch (const ApiError& e)
    {
      CHECK(e.Status() == 500);
      CHECK(std::string(e.what()).find("SQLITE_BUSY") != std::string::npos);
    }
    CHECK(sleeps == Api::kRetries);
  }
}

TEST_CASE("api: other errors are not retried")
{
  FakeHttp http;
  Api api(http, "http://h:8888");
  int sleeps = 0;
  api.sleep = [&](int) { ++sleeps; };

  http.responses = {{500, R"({"code":500,"errors":"IPCTimeout"})"}};
  CHECK_THROWS_AS(api.Channels(), ApiError);
  http.responses = {{0, ""}};
  try
  {
    api.Version();
    FAIL("expected ApiError");
  }
  catch (const ApiError& e)
  {
    CHECK(e.Status() == 0);
  }
  http.responses = {{404, "<html>"}};
  CHECK_THROWS_AS(api.Version(), ApiError);
  CHECK(sleeps == 0);
}

TEST_CASE("api: recordings in pages, signature, delete")
{
  FakeHttp http;
  Api api(http, "http://h:8888");
  api.sleep = [](int) {};

  SECTION("pages until total")
  {
    std::string page1 = R"({"total":101,"records":[)";
    for (int i = 0; i < Api::kRecordedPageSize; ++i)
      page1 += (i ? "," : "") + std::string(R"({"id":)") + std::to_string(200 - i) +
               R"(,"startAt":0,"endAt":1000,"name":"n","isRecording":false,"isEncoding":false,"isProtected":false})";
    page1 += "]}";
    http.responses = {{200, R"({"total":1,"records":[{"id":300,"startAt":0,"endAt":1,"name":"rec",
                        "isRecording":true,"isEncoding":false,"isProtected":false}]})"},
                      {200, page1},
                      {200, R"({"total":101,"records":[{"id":5,"startAt":0,"endAt":1,"name":"x",
                        "isRecording":false,"isEncoding":false,"isProtected":false}]})"}};
    const auto all = api.Recorded();
    CHECK(all.size() == 102);
    CHECK(all.front().id == 300); // in progress first
    CHECK(all.front().isRecording);
    CHECK(all.back().id == 5);
    CHECK(http.urls.at(0) == "http://h:8888/api/recording?isHalfWidth=true&offset=0&limit=100");
    CHECK(http.urls.at(1) == "http://h:8888/api/recorded?isHalfWidth=true&offset=0&limit=100");
    CHECK(http.urls.at(2) == "http://h:8888/api/recorded?isHalfWidth=true&offset=100&limit=100");
  }
  SECTION("a recording finishing between the two requests is listed once, as finished")
  {
    http.responses = {
        {200, R"({"total":1,"records":[{"id":7,"startAt":0,"endAt":1,"name":"x",
          "isRecording":true,"isEncoding":false,"isProtected":false}]})"},
        {200, R"({"total":1,"records":[{"id":7,"startAt":0,"endAt":1,"name":"x",
          "isRecording":false,"isEncoding":false,"isProtected":false}]})"}};
    const auto all = api.Recorded();
    REQUIRE(all.size() == 1);
    CHECK_FALSE(all[0].isRecording);
  }
  SECTION("signature")
  {
    http.responses = {
        {200, R"({"total":9,"records":[{"id":12,"startAt":0,"endAt":1,"name":"x",
          "isRecording":true,"isEncoding":false,"isProtected":false}]})"},
        {200, R"({"total":2,"records":[]})"},
        {200, R"({"runningItems":[{}],"waitItems":[{},{}]})"}};
    const RecordedSignature s = api.Signature();
    CHECK(s.total == 9);
    CHECK(s.newestId == 12);
    CHECK(s.recording == 2);
    CHECK(s.encoding == 3);
    CHECK(s != RecordedSignature{});
  }
  SECTION("delete is a DELETE and never retried")
  {
    http.responses = {kBusy};
    CHECK_THROWS_AS(api.DeleteRecorded(7), ApiError);
    CHECK(http.urls == std::vector<std::string>{"DELETE http://h:8888/api/recorded/7"});
  }
  CHECK(api.VideoUrl(84) == "http://h:8888/api/videos/84");
  CHECK(api.ThumbnailUrl(80) == "http://h:8888/api/thumbnails/80");
}

TEST_CASE("api: reservations")
{
  FakeHttp http;
  Api api(http, "http://h:8888");
  api.sleep = [](int) {};

  SECTION("add returns the new id and is never retried")
  {
    http.responses = {{201, R"({"reserveId":42})"}};
    CHECK(api.AddReserve(EpgReserveBody(321124096003231ULL)) == 42);
    CHECK(http.urls.at(0) == "POST http://h:8888/api/reserves");
    CHECK(http.bodies.at(0) == R"({"allowEndLack":true,"programId":321124096003231})");

    http.responses = {kBusy};
    CHECK_THROWS_AS(api.AddReserve("{}"), ApiError);
    CHECK(http.urls.size() == 2);
  }
  SECTION("delete")
  {
    http.responses = {{200, R"({"code":200})"}};
    api.DeleteReserve(10);
    CHECK(http.urls.at(0) == "DELETE http://h:8888/api/reserves/10");
  }
  SECTION("fingerprint covers a week back (recording now) to 60 days ahead")
  {
    http.responses = {{200, R"({"normal":[],"conflicts":[],"skips":[],"overlaps":[]})"}};
    api.ReservesFingerprint(1000000);
    CHECK(http.urls.at(0) ==
          "http://h:8888/api/reserves/lists?startAt=395200000&endAt=6184000000");
  }
}

TEST_CASE("api: rules")
{
  FakeHttp http;
  Api api(http, "http://h:8888");
  api.sleep = [](int) {};
  const std::string rule = R"({"id":5,"searchOption":{"keyword":"ひよっこ","name":true,
    "channelIds":[3211240960]},"reserveOption":{"enable":true}})";

  http.responses = {{200, rule}};
  const Rule r = api.GetRule(5);
  CHECK(r.id == 5);
  CHECK(r.keyword == "ひよっこ");
  CHECK(http.urls.at(0) == "http://h:8888/api/rules/5");

  http.responses = {{201, R"({"ruleId":6})"}, {200, "{}"}, {200, "{}"}, {200, "{}"}, {200, "{}"},
                    {200, "{}"}};
  CHECK(api.AddRule("{}") == 6);
  api.UpdateRule(6, R"({"a":1})");
  api.SetRuleEnabled(6, false);
  api.DeleteRule(6);
  api.UnskipReserve(9);
  CHECK(http.urls.at(1) == "POST http://h:8888/api/rules");
  CHECK(http.urls.at(2) == "PUT http://h:8888/api/rules/6");
  CHECK(http.bodies.at(1) == R"({"a":1})");
  CHECK(http.urls.at(3) == "PUT http://h:8888/api/rules/6/disable");
  CHECK(http.urls.at(4) == "DELETE http://h:8888/api/rules/6");
  CHECK(http.urls.at(5) == "DELETE http://h:8888/api/reserves/9/skip");
}
