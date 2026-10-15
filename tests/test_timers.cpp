// Reservations -> Kodi timers against a real /api/reserves capture (tests/fixtures/prod).

#include "epgstation/Channels.h"
#include "epgstation/Programs.h"
#include "epgstation/Timers.h"

#include "Fixtures.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace epgstation;

TEST_CASE("timers: production reservations")
{
  const auto reserves = ParseReserves(ReadFile("prod/reserves.json"));
  REQUIRE(reserves.size() == 24);
  int skipped = 0;
  for (const auto& r : reserves)
  {
    CAPTURE(r.id, r.name);
    const KodiTimer t = ToKodiTimer(r, 0);
    CHECK(t.clientIndex == static_cast<unsigned int>(r.id));
    CHECK(t.type == timer_type::kRuleOnce); // all 24 come from rules
    CHECK(t.ruleId == r.ruleId);
    CHECK(EpgsIdFromUid(t.channelUid) == r.channelId);
    CHECK(ProgramId(t.channelUid, t.epgUid) == r.programId);
    CHECK(t.end > t.start);
    CHECK_FALSE(t.title.empty());
    skipped += t.state == timer_state::kDisabled ? 1 : 0;
  }
  CHECK(skipped == 2);
}

TEST_CASE("timers: types and states")
{
  Reserve r;
  r.id = 7;
  r.channelId = 3211240960ULL;
  r.programId = 321124096003231ULL;
  r.startAt = 1000 * 1000;
  r.endAt = 1300 * 1000;
  r.name = "ニュース[字]";

  CHECK(ToKodiTimer(r, 0).type == timer_type::kEpgOnce);
  CHECK(ToKodiTimer(r, 0).state == timer_state::kScheduled);
  CHECK(ToKodiTimer(r, 1000).state == timer_state::kRecording);
  CHECK(ToKodiTimer(r, 1299).state == timer_state::kRecording);
  CHECK(ToKodiTimer(r, 1300).state == timer_state::kScheduled);

  Reserve conflict = r;
  conflict.isConflict = true;
  CHECK(ToKodiTimer(conflict, 0).state == timer_state::kConflictNok);
  Reserve skip = r;
  skip.isSkip = true;
  skip.isConflict = true;
  CHECK(ToKodiTimer(skip, 0).state == timer_state::kDisabled);

  Reserve manual = r;
  manual.isTimeSpecified = true;
  manual.programId = 0;
  const KodiTimer m = ToKodiTimer(manual, 0);
  CHECK(m.type == timer_type::kManualOnce);
  CHECK(m.epgUid == 0);
  CHECK(m.channelUid == KodiUid(32112, 40960));
}

TEST_CASE("timers: request bodies")
{
  const auto e = nlohmann::json::parse(EpgReserveBody(321124096003231ULL));
  CHECK(e["programId"] == 321124096003231ULL);
  CHECK(e["allowEndLack"] == true);

  const auto m = nlohmann::json::parse(ManualReserveBody("手動", 400101, 1000, 1300));
  CHECK(m["timeSpecifiedOption"]["name"] == "手動");
  CHECK(m["timeSpecifiedOption"]["channelId"] == 400101);
  CHECK(m["timeSpecifiedOption"]["startAt"] == 1000000);
  CHECK(m["timeSpecifiedOption"]["endAt"] == 1300000);
  CHECK_FALSE(m.contains("programId"));

  CHECK_THROWS(ParseReserves("[]"));
  CHECK(ParseReserves(R"({"reserves":[],"total":0})").empty());
}
