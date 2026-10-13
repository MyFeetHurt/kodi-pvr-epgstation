// EPG mapping against real 3-day /api/schedules/{channelId} captures (tests/fixtures/{dev,prod}).

#include "epgstation/Channels.h"
#include "epgstation/Programs.h"

#include <filesystem>
#include <fstream>
#include <set>
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

time_t FixtureStart()
{
  return static_cast<time_t>(std::stoll(ReadFile("schedule_start_ms.txt")) / 1000);
}
} // namespace

TEST_CASE("programs: real schedules map to clean Kodi EPG tags")
{
  const std::string set = GENERATE("dev", "prod");
  const std::string channel = GENERATE("3211240960", "400101", "700227");
  CAPTURE(set, channel);

  const auto programs = ParseChannelSchedule(ReadFile(set + "/schedule_" + channel + ".json"));
  REQUIRE(programs.size() > 30);

  const time_t start = FixtureStart();
  const time_t end = start + 3 * 86400;
  const auto tags = ToKodiEpg(programs, start, end);

  std::set<uint64_t> ids;
  for (const auto& p : programs)
    ids.insert(p.id);
  CHECK(tags.size() == ids.size()); // the slice-boundary duplicates are gone

  std::set<unsigned int> broadcastIds;
  const uint64_t channelId = std::stoull(channel);
  const unsigned int uid = KodiUid(static_cast<int>(channelId / 100000),
                                   static_cast<int>(channelId % 100000));
  for (size_t i = 0; i < tags.size(); ++i)
  {
    const auto& t = tags[i];
    broadcastIds.insert(t.broadcastId);
    CHECK(t.broadcastId > 0);
    CHECK(ids.count(ProgramId(uid, t.broadcastId)) == 1); // maps back to the EPGStation id
    CHECK(t.end > t.start);
    CHECK(t.end > start);
    CHECK(t.start < end);
    CHECK_FALSE(t.title.empty());
    CHECK(t.genreDescription.find(",,") == std::string::npos);
    if (i > 0)
      CHECK(tags[i - 1].start <= t.start);
  }
  CHECK(broadcastIds.size() == tags.size());
}

TEST_CASE("programs: genres, plot and flags of known programmes")
{
  const auto programs = ParseChannelSchedule(ReadFile("prod/schedule_3211240960.json"));
  const auto tags = ToKodiEpg(programs, 0, INT32_MAX);

  SECTION("every NHK総合 programme has a genre and a colour")
  {
    for (const auto& t : tags)
    {
      CAPTURE(t.title);
      CHECK_FALSE(t.genreDescription.empty());
      CHECK(t.genreType != 0);
    }
  }
  SECTION("[新] and [終] in titles become Kodi flags")
  {
    int newCount = 0, finaleCount = 0;
    for (const auto& t : tags)
    {
      CHECK(((t.flags & flags::kNew) != 0) == (t.title.find("[新]") != std::string::npos));
      newCount += (t.flags & flags::kNew) ? 1 : 0;
      finaleCount += (t.flags & flags::kFinale) ? 1 : 0;
    }
    CHECK(newCount == 3);
    CHECK(finaleCount == 1);
  }
  SECTION("plot is description followed by extended")
  {
    bool sawExtended = false;
    for (const auto& p : programs)
    {
      if (p.extended.empty() || p.description.empty())
        continue;
      const auto t = ToKodiEpgTag(p);
      CHECK(t.plotOutline == p.description);
      CHECK(t.plot == p.description + "\n\n" + p.extended);
      sawExtended = true;
    }
    CHECK(sawExtended);
  }
}

TEST_CASE("programs: CS films with the genre only in genre2 still get it")
{
  const auto programs = ParseChannelSchedule(ReadFile("prod/schedule_700227.json"));
  int onlySecond = 0;
  for (const auto& p : programs)
  {
    if (p.genres[0].genre >= 0)
      continue;
    REQUIRE(p.genres[1].genre == 6);
    ++onlySecond;
    const auto t = ToKodiEpgTag(p);
    CHECK(t.genreType == 0x10);
    CHECK(t.genreDescription == "映画（洋画）");
  }
  CHECK(onlySecond > 0);

  Program none;
  const auto t = ToKodiEpgTag(none);
  CHECK(t.genreType == 0);
  CHECK(t.genreDescription.empty());
}

TEST_CASE("programs: genre names and mapping")
{
  CHECK(GenreName({6, 0}) == "映画（洋画）");
  CHECK(GenreName({7, 0}) == "アニメ・特撮（国内アニメ）");
  CHECK(GenreName({3, 15}) == "ドラマ（その他）");
  CHECK(GenreName({3, 9}) == "ドラマ"); // undefined sub genre: main name only
  CHECK(GenreName({-1, -1}).empty());
  CHECK(GenreName({16, 0}).empty());

  Program p;
  p.genres[0] = {2, 5};
  p.genres[1] = {5, 6};
  p.genres[2] = {2, 5}; // duplicate
  CHECK(GenreDescription(p) == "情報・ワイドショー（グルメ・料理）,バラエティ（料理バラエティ）");

  CHECK(KodiGenreType(6) == 0x10); // movie
  CHECK(KodiGenreType(3) == 0x10); // drama
  CHECK(KodiGenreType(7) == 0x50); // anime: children's/youth
  CHECK(KodiGenreType(1) == 0x40); // sports
  CHECK(KodiGenreType(14) == 0);
  CHECK(KodiGenreType(-1) == 0);
}

TEST_CASE("programs: ids, flags and days")
{
  CHECK(BroadcastId(321124096030419ULL) == (0x10000u | 30419u));
  CHECK(BroadcastId(321124096000000ULL) == 0x10000u); // eventId 0 still > 0
  CHECK(ProgramId(KodiUid(32112, 40960), BroadcastId(321124096030419ULL)) == 321124096030419ULL);
  CHECK(ProgramId(KodiUid(4, 101), BroadcastId(40010100123ULL)) == 40010100123ULL);

  CHECK(TitleFlags("ニュース") == 0);
  CHECK(TitleFlags("[新]ドラマ[字]") == flags::kNew);
  CHECK(TitleFlags("[生]サッカー[終]") == (flags::kLive | flags::kFinale));
  CHECK(TitleFlags("[初]映画") == flags::kPremiere);

  CHECK(ScheduleDays(0, 0) == 1);
  CHECK(ScheduleDays(0, 1) == 1);
  CHECK(ScheduleDays(0, 86400) == 1);
  CHECK(ScheduleDays(0, 86401) == 2);
  CHECK(ScheduleDays(1000, 1000 + 3 * 86400) == 3);
}

TEST_CASE("programs: bad JSON is an error, not a crash")
{
  CHECK_THROWS(ParseChannelSchedule("{\"code\":500}"));
  CHECK_THROWS(ParseChannelSchedule("[{\"channel\":{}}]"));
  CHECK_THROWS(ParseChannelSchedule("[{\"programs\":[{\"id\":1}]}]"));
  CHECK(ParseChannelSchedule("[]").empty());
  CHECK(ParseChannelSchedule("[{\"programs\":[]}]").empty());
}

TEST_CASE("programs: EPG changes between two snapshots of the near future")
{
  const auto programs = ParseChannelSchedule(ReadFile("prod/schedule_3211240960.json"));
  REQUIRE(programs.size() > 10);
  const int64_t t0 = programs[3].startAt;
  const int64_t hour = 3600 * 1000;

  ScheduleSnapshot before{t0, t0 + 6 * hour, programs};
  SECTION("same guide, window moved on 10 minutes: no change")
  {
    ScheduleSnapshot after{t0 + 10 * 60000, t0 + 6 * hour + 10 * 60000, programs};
    CHECK(ChangedChannels(before, after).empty());
  }
  SECTION("an overrun inside the shared window is a change of that channel")
  {
    auto changed = programs;
    for (auto& p : changed)
      if (p.startAt >= t0 + hour && p.startAt < t0 + 2 * hour)
        p.startAt += 10 * 60000, p.endAt += 10 * 60000;
    ScheduleSnapshot after{t0, t0 + 6 * hour, changed};
    CHECK(ChangedChannels(before, after) == std::vector<uint64_t>{3211240960ULL});
  }
  SECTION("changes outside the shared window do not count")
  {
    auto changed = programs;
    for (auto& p : changed)
      if (p.startAt >= t0 + 7 * hour)
        p.name += "!";
    ScheduleSnapshot after{t0, t0 + 6 * hour, changed};
    CHECK(ChangedChannels(before, after).empty());
  }
  SECTION("only the description, extended text or genre changed")
  {
    for (int which = 0; which < 3; ++which)
    {
      auto changed = programs;
      for (auto& p : changed)
      {
        if (p.startAt < t0 + hour || p.startAt >= t0 + 2 * hour)
          continue;
        if (which == 0)
          p.description += "（内容変更）";
        else if (which == 1)
          p.extended += "\n◇出演者追加";
        else
          p.genres[0] = {7, 0};
      }
      CAPTURE(which);
      CHECK(ChangedChannels(before, ScheduleSnapshot{t0, t0 + 6 * hour, changed}).size() == 1);
    }
  }
  SECTION("a renamed programme and a channel that vanished")
  {
    auto changed = programs;
    for (auto& p : changed)
      if (p.startAt >= t0 && p.startAt < t0 + hour)
        p.name = "[臨]" + p.name;
    ScheduleSnapshot after{t0, t0 + 6 * hour, changed};
    CHECK(ChangedChannels(before, after).size() == 1);
    CHECK(ChangedChannels(before, ScheduleSnapshot{t0, t0 + 6 * hour, {}}).size() == 1);
  }
}
