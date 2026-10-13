// EPGStation rules <-> Kodi timer rules, against the real production rule (tests/fixtures/prod).

#include "epgstation/Programs.h"
#include "epgstation/Recordings.h"
#include "epgstation/Rules.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

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
} // namespace

TEST_CASE("rules: the production rule is shown read-only (genre, BS+CS only)")
{
  const auto rules = ParseRules(ReadFile("prod/rules.json"));
  REQUIRE(rules.size() == 1);
  const Rule& r = rules[0];
  CHECK(r.id == 2);
  CHECK(r.keyword == "吹替");
  CHECK(r.enabled);
  CHECK(r.fullText);
  CHECK(r.kodiWeekdays == 0x7F);
  CHECK(r.anyTime);
  CHECK_FALSE(r.editable); // genres + not GR: Kodi's dialog would lose them

  // An update keeps what Kodi does not know (genres, saveOption.directory).
  RuleEdit e;
  e.keyword = "吹替版";
  e.fullText = true;
  e.enabled = false;
  const auto body = nlohmann::json::parse(UpdatedRuleBody(r.raw, e));
  CHECK_FALSE(body.contains("id"));
  CHECK(body["searchOption"]["keyword"] == "吹替版");
  CHECK(body["searchOption"]["genres"][0]["genre"] == 6);
  CHECK(body["saveOption"]["directory"] == "fukikae");
  CHECK(body["reserveOption"]["enable"] == false);
}

TEST_CASE("rules: editable ones and the round trip through Kodi's fields")
{
  RuleEdit e;
  e.keyword = "ひよっこ";
  e.channelId = 3211240960ULL;
  e.kodiWeekdays = 0x3F; // Monday-Saturday
  e.anyTime = false;
  e.startHour = 12;
  e.endHour = 13;
  e.avoidDuplicate = true;
  const std::string body = NewRuleBody(e);
  const auto j = nlohmann::json::parse(body);
  CHECK(j["isTimeSpecification"] == false);
  CHECK(j["searchOption"]["channelIds"][0] == 3211240960ULL);
  CHECK_FALSE(j["searchOption"].contains("GR"));
  CHECK(j["searchOption"]["times"][0]["week"] == 0x7E); // EPGStation: Mon..Sat = bits 1..6
  CHECK(j["searchOption"]["times"][0]["start"] == 12);
  CHECK(j["searchOption"]["times"][0]["range"] == 1);
  CHECK(j["reserveOption"]["avoidDuplicate"] == true);

  // As EPGStation would list it.
  nlohmann::json listed = j;
  listed["id"] = 5;
  const auto rules = ParseRules(nlohmann::json{{"rules", {listed}}, {"total", 1}}.dump());
  const Rule& r = rules.at(0);
  CHECK(r.editable);
  CHECK(r.keyword == "ひよっこ");
  CHECK(r.channelId == 3211240960ULL);
  CHECK(r.kodiWeekdays == 0x3F);
  CHECK_FALSE(r.anyTime);
  CHECK(r.startHour == 12);
  CHECK(r.endHour == 13);
  CHECK_FALSE(r.fullText);
  CHECK(r.avoidDuplicate);

  RuleEdit any;
  any.keyword = "ドキュメント72時間";
  const auto a = nlohmann::json::parse(NewRuleBody(any));
  CHECK(a["searchOption"]["GR"] == true);
  CHECK(a["searchOption"]["BS"] == true);
  CHECK(a["searchOption"]["CS"] == true);
  CHECK_FALSE(a["searchOption"].contains("channelIds"));
  CHECK_FALSE(a["searchOption"]["times"][0].contains("start"));
  nlohmann::json listedAny = a;
  listedAny["id"] = 6;
  const auto anyRules = ParseRules(nlohmann::json{{"rules", {listedAny}}}.dump());
  CHECK(anyRules.at(0).editable);
  CHECK(anyRules.at(0).channelId == 0);
  CHECK(anyRules.at(0).anyTime);

  // SKY too: Kodi's "any channel" would drop it on save, so read-only.
  nlohmann::json sky = listedAny;
  sky["searchOption"]["SKY"] = true;
  CHECK_FALSE(ParseRules(nlohmann::json{{"rules", {sky}}}.dump()).at(0).editable);
}

TEST_CASE("rules: weekday bits")
{
  CHECK(KodiWeekdays(0x01) == 0x40); // Sunday
  CHECK(KodiWeekdays(0x02) == 0x01); // Monday
  CHECK(KodiWeekdays(0x40) == 0x20); // Saturday
  CHECK(KodiWeekdays(0x7F) == 0x7F);
  for (unsigned int w = 0; w < 0x80; ++w)
    CHECK(KodiWeekdays(EpgsWeek(w)) == (w ? w : 0x7F));
}

TEST_CASE("rules: series keywords from programme titles")
{
  CHECK(SeriesKeyword("【連続テレビ小説】ひよっこ(48)「夏の思い出はメロン色」[解][字]") == "ひよっこ");
  CHECK(SeriesKeyword("【連続テレビ小説】ブラッサム(12)第3週「化粧と、初恋」[解][字][再]") == "ブラッサム");
  CHECK(SeriesKeyword("[新]ドラマ 相棒 第3話「容疑者」[字]") == "相棒");
  CHECK(SeriesKeyword("歴史探偵 選 竹中半兵衛と黒田官兵衛 知られざる涙の絆[解][字]") == "歴史探偵");
  CHECK(SeriesKeyword("NHKスペシャル 靖国神社 スクープ資料“空白の戦後史\" 秘録に何が[字][再]") ==
        "NHKスペシャル");
  CHECK(SeriesKeyword("京都 嵯峨嵐山 錦秋の紅葉狩り") == "京都 嵯峨嵐山");
  CHECK(SeriesKeyword("[字]THE SIN 罪") == "THE SIN");
  CHECK(SeriesKeyword("[HV][字][映]劇場版 ほんとにあった!呪いのビデオ100") == "ほんとにあった!呪いのビデオ100");
  CHECK(SeriesKeyword("ロバート秋山&富士彦のオレ達商店GUY #22") == "ロバート秋山&富士彦のオレ達商店GUY");
  CHECK(SeriesKeyword("釣りびと万歳 ハイスピードで誘え!大サワラに挑む~松永有紗 東京湾~[字][再]") ==
        "釣りびと万歳");
  CHECK(SeriesKeyword("ニュース[字]") == "ニュース");
  CHECK(SeriesKeyword("100分de名著 オルダス・ハクスリー“すばらしい新世界\"[終][解][字]") ==
        "100分de名著");
  CHECK(SeriesKeyword("[映]ダーティハリー[吹]日曜洋画劇場 追録ノーカット版") == "ダーティハリー");
  CHECK(SeriesKeyword("映画「容疑者 室井慎次」[字][解]◆最新作公開記念!") == "容疑者 室井慎次");
  CHECK(SeriesKeyword("韓◆ワーキング・ガール[字]") == "ワーキング・ガール");
  CHECK(SeriesKeyword("ニュース") == "ニュース");
  CHECK(SeriesKeyword("[字]") == "");
  CHECK(SeriesKeyword("第9") == "第9"); // never cut a whole title away

  // Every real title gives a non-empty keyword that the title itself still matches.
  const std::string set = GENERATE("dev", "prod");
  const auto page = ParseRecorded(ReadFile(set + "/recorded.json"));
  for (const auto& rec : page.records)
  {
    const std::string k = SeriesKeyword(rec.program.name);
    CAPTURE(rec.program.name, k);
    CHECK_FALSE(k.empty());
    CHECK(rec.program.name.find(k) != std::string::npos);
  }
}
