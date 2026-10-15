// Channel mapping against real /api/channels captures (tests/fixtures/{dev,prod}).

#include "epgstation/Channels.h"

#include <set>

#include "Fixtures.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace epgstation;

namespace
{
const KodiChannel* Find(const std::vector<KodiChannel>& channels, const std::string& name)
{
  for (const auto& c : channels)
    if (c.name == name)
      return &c;
  return nullptr;
}
} // namespace

TEST_CASE("channels: parse and map the captured channel lists")
{
  const std::string set = GENERATE("dev", "prod");
  CAPTURE(set);
  const auto channels = ParseChannels(ReadFile(set + "/channels.json"));
  REQUIRE(channels.size() == 147);

  const auto kodi = ToKodiChannels(channels);
  // 122 TV + 1 radio; 24 data/one-seg services are left out.
  CHECK(kodi.size() == 123);

  std::set<unsigned int> uids;
  for (const auto& c : kodi)
  {
    uids.insert(c.uid);
    CHECK(EpgsIdFromUid(c.uid) == c.epgsId); // uid maps back to the EPGStation id
    CHECK(c.number > 0);
    CHECK_FALSE(c.name.empty());
  }
  CHECK(uids.size() == kodi.size());

  SECTION("Japanese channel numbers")
  {
    const auto* nhk1 = Find(kodi, "NHK総合1・大阪");
    REQUIRE(nhk1);
    CHECK(nhk1->number == 11);
    CHECK(nhk1->group == "GR");
    CHECK(nhk1->hasLogo);
    REQUIRE(Find(kodi, "NHK総合2・大阪"));
    CHECK(Find(kodi, "NHK総合2・大阪")->number == 12);
    REQUIRE(Find(kodi, "NHKEテレ1大阪"));
    CHECK(Find(kodi, "NHKEテレ1大阪")->number == 21);
    REQUIRE(Find(kodi, "MBS"));
    CHECK(Find(kodi, "MBS")->number == 41);
    REQUIRE(Find(kodi, "NHK BS"));
    CHECK(Find(kodi, "NHK BS")->number == 101);
    CHECK(Find(kodi, "NHK BS")->group == "BS");
  }

  SECTION("radio and data services")
  {
    const auto* radio = Find(kodi, "放送大学ラジオ");
    REQUIRE(radio);
    CHECK(radio->radio);
    CHECK(radio->group == "BS");
    CHECK_FALSE(Find(kodi, "NHK携帯G・大阪")); // one-seg (type 0xC0)
    CHECK_FALSE(Find(kodi, "Gガイド")); // data
  }

  SECTION("groups by channel type, radio separate, members in channel order")
  {
    const auto groups = ToKodiGroups(kodi);
    REQUIRE(groups.size() == 4);
    CHECK((groups[0].name == "GR" && !groups[0].radio));
    CHECK((groups[1].name == "BS" && !groups[1].radio));
    size_t members = 0;
    for (const auto& g : groups)
      members += g.members.size();
    CHECK(members == kodi.size());
    CHECK(groups[0].members.front() == Find(kodi, "NHK総合1・大阪")->uid);
    bool radioBs = false;
    for (const auto& g : groups)
      radioBs = radioBs || (g.name == "BS" && g.radio && g.members.size() == 1);
    CHECK(radioBs);
  }
}

TEST_CASE("channels: uid round trip and limits")
{
  CHECK(KodiUid(32112, 40960) == (32112u << 16 | 40960u));
  CHECK(EpgsIdFromUid(KodiUid(32112, 40960)) == 3211240960ULL);
  CHECK(EpgsIdFromUid(KodiUid(65535, 65535)) == 6553565535ULL); // beyond 32-bit EPGStation ids
  CHECK(EpgsIdFromUid(KodiUid(4, 101)) == 400101ULL);
}

TEST_CASE("channels: GR without a remote key falls back to the service id")
{
  Channel c;
  c.channelType = "GR";
  c.serviceId = 1024;
  CHECK(ChannelNumber(c) == 1024);
  c.remoteControlKeyId = 3;
  c.serviceId = 1026; // & 7 == 2
  CHECK(ChannelNumber(c) == 33);
}

TEST_CASE("channels: bad JSON is an error, not a crash")
{
  CHECK_THROWS(ParseChannels("{\"code\":500}"));
  CHECK_THROWS(ParseChannels("not json"));
  CHECK_THROWS(ParseChannels("[{\"name\":\"no ids\"}]"));
  CHECK(ParseChannels("[]").empty());
}
