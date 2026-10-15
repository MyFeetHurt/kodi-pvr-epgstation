// Against a real EPGStation (opt-in, never in the unit tests):
//   EPGS_URL=http://dev-host:8888 build/tests/live_tests                  # read-only checks
//   EPGS_URL=... EPGS_LIVE_WRITE=1 build/tests/live_tests                 # + rules and reservations
// The write checks create only rules and reservations named "pvrlive-<random>" at least two days
// ahead (no tuner is used) and delete them again. Never point them at a production server.

#include "epgstation/Api.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <random>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using namespace epgstation;

namespace
{
class CurlHttp : public IHttp
{
public:
  HttpResponse Get(const std::string& url) override { return Request("GET", url, nullptr); }
  HttpResponse Delete(const std::string& url) override { return Request("DELETE", url, nullptr); }
  HttpResponse Post(const std::string& url, const std::string& body) override
  {
    return Request("POST", url, &body);
  }
  HttpResponse Put(const std::string& url, const std::string& body) override
  {
    return Request("PUT", url, &body);
  }

private:
  static size_t Append(char* data, size_t size, size_t n, void* out)
  {
    static_cast<std::string*>(out)->append(data, size * n);
    return size * n;
  }
  HttpResponse Request(const char* method, const std::string& url, const std::string* body)
  {
    HttpResponse r;
    CURL* curl = curl_easy_init();
    curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Append);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
    if (body)
    {
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());
    }
    if (curl_easy_perform(curl) == CURLE_OK)
    {
      long status = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
      r.status = static_cast<int>(status);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return r;
  }
};

std::string ServerUrl()
{
  const char* url = std::getenv("EPGS_URL");
  if (!url || !*url)
    SKIP("EPGS_URL not set");
  return url;
}

void NeedWrite()
{
  const char* write = std::getenv("EPGS_LIVE_WRITE");
  if (!write || std::string(write) != "1")
    SKIP("EPGS_LIVE_WRITE=1 not set");
}

std::string UniqueName()
{
  std::random_device rd;
  return "pvrlive-" + std::to_string(rd() % 1000000000);
}

uint64_t FirstGrChannel(Api& api)
{
  for (const auto& c : api.Channels())
    if (c.channelType == "GR" && IsViewable(c))
      return c.id;
  FAIL("no GR channel");
  return 0;
}

// Rule reservations appear a moment after the rule is saved.
template<typename Pred>
std::vector<Reserve> WaitForReserves(Api& api, Pred pred)
{
  for (int i = 0; i < 40; ++i)
  {
    std::vector<Reserve> found;
    for (const auto& r : api.Reserves())
      if (pred(r))
        found.push_back(r);
    if (!found.empty())
      return found;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  return {};
}
} // namespace

TEST_CASE("live: everything the server lists parses")
{
  CurlHttp http;
  Api api(http, ServerUrl());
  INFO(api.BaseUrl());
  CHECK_FALSE(api.Version().empty());

  const auto channels = api.Channels();
  REQUIRE_FALSE(channels.empty());
  const auto kodi = ToKodiChannels(channels);
  CHECK_FALSE(kodi.empty());

  // The whole guide, channel by channel, as Kodi asks for it.
  const time_t now = std::time(nullptr);
  size_t programmes = 0;
  for (const auto& c : kodi)
  {
    INFO(c.name);
    const auto tags = ToKodiEpg(api.ChannelSchedule(c.epgsId, int64_t{now} * 1000, 8), now,
                                now + 8 * 86400);
    programmes += tags.size();
    for (const auto& t : tags)
      CHECK(t.end > t.start);
  }
  CHECK(programmes > 0);

  for (const auto& r : api.Recorded())
    CHECK_FALSE(ToKodiRecording(r, true).recordingId.empty());
  for (const auto& r : api.Reserves())
    CHECK(ToKodiTimer(r, now).clientIndex == static_cast<unsigned int>(r.id));
  CHECK_NOTHROW(api.Rules());
  CHECK_NOTHROW(api.Signature());
  CHECK_NOTHROW(api.Storages());
  CHECK_NOTHROW(api.Config());
}

TEST_CASE("live: a rule edited the way Kodi edits it")
{
  CurlHttp http;
  Api api(http, ServerUrl());
  NeedWrite();

  RuleEdit e;
  e.keyword = UniqueName(); // matches nothing: no reservations
  e.channelId = FirstGrChannel(api);
  e.kodiWeekdays = 0x1F; // Monday-Friday
  e.anyTime = false;
  e.startHour = 22;
  e.endHour = 1; // across midnight
  e.avoidDuplicate = true;
  const int id = api.AddRule(NewRuleBody(e));

  Rule r = api.GetRule(id);
  CHECK(r.editable);
  CHECK(r.keyword == e.keyword);
  CHECK(r.channelId == e.channelId);
  CHECK(r.kodiWeekdays == 0x1F);
  CHECK(r.startHour == 22);
  CHECK(r.endHour == 1);
  CHECK(r.avoidDuplicate);

  e.channelId = 0; // any channel
  e.anyTime = true;
  e.fullText = true;
  e.avoidDuplicate = false;
  CHECK_NOTHROW(api.UpdateRule(id, UpdatedRuleBody(r.raw, e)));
  r = api.GetRule(id);
  CHECK(r.editable);
  CHECK(r.channelId == 0);
  CHECK(r.anyTime);
  CHECK(r.fullText);
  CHECK_FALSE(r.avoidDuplicate);

  CHECK_NOTHROW(api.SetRuleEnabled(id, false));
  CHECK_FALSE(api.GetRule(id).enabled);
  CHECK_NOTHROW(api.SetRuleEnabled(id, true));
  CHECK(api.GetRule(id).enabled);
  api.DeleteRule(id);
  CHECK_THROWS(api.GetRule(id));
}

TEST_CASE("live: duplicate prevention switched off on a web UI rule with a period")
{
  CurlHttp http;
  Api api(http, ServerUrl());
  NeedWrite();

  // What EPGStation's web UI saves with 重複を避ける and a period.
  nlohmann::json web = nlohmann::json::parse(NewRuleBody(RuleEdit{UniqueName()}));
  web["reserveOption"]["avoidDuplicate"] = true;
  web["reserveOption"]["periodToAvoidDuplicate"] = 7;
  const int id = api.AddRule(web.dump());
  const Rule r = api.GetRule(id);
  REQUIRE(r.editable);

  RuleEdit off{r.keyword};
  off.avoidDuplicate = false;
  CHECK_NOTHROW(api.UpdateRule(id, UpdatedRuleBody(r.raw, off)));
  CHECK_FALSE(api.GetRule(id).avoidDuplicate);
  api.DeleteRule(id);
}

TEST_CASE("live: a manual timer replaced the way Kodi edits it")
{
  CurlHttp http;
  Api api(http, ServerUrl());
  NeedWrite();

  const uint64_t channel = FirstGrChannel(api);
  const std::string name = UniqueName();
  // Three days ahead at 04:00 local, 5 minutes.
  const time_t now = std::time(nullptr);
  std::tm day{};
  localtime_r(&now, &day);
  day.tm_mday += 3;
  day.tm_hour = 4;
  day.tm_min = day.tm_sec = 0;
  const time_t start = std::mktime(&day);

  const int first = api.AddReserve(ManualReserveBody(name, channel, start, start + 300));
  // UpdateTimer: the new reservation first (it overlaps the old one), then delete the old one.
  const int second = api.AddReserve(ManualReserveBody(name, channel, start, start + 600));
  api.DeleteReserve(first);

  std::vector<Reserve> mine;
  for (const auto& r : api.Reserves())
    if (r.name == name)
      mine.push_back(r);
  REQUIRE(mine.size() == 1);
  CHECK(mine[0].id == second);
  const KodiTimer t = ToKodiTimer(mine[0], now);
  CHECK(t.type == timer_type::kManualOnce);
  CHECK(t.end - t.start == 600);
  CHECK(t.channelUid == KodiUidFromEpgsId(channel));
  api.DeleteReserve(second);
}

TEST_CASE("live: a rule's reservation disabled and enabled again (skip / unskip)")
{
  CurlHttp http;
  Api api(http, ServerUrl());
  NeedWrite();

  // A programme two to six days ahead on one channel, matched by its exact name.
  const uint64_t channel = FirstGrChannel(api);
  const time_t now = std::time(nullptr);
  const auto programmes = api.ChannelSchedule(channel, (int64_t{now} + 2 * 86400) * 1000, 4);
  const Program* target = nullptr;
  for (const auto& p : programmes)
    if (p.name.size() > 12 && p.endAt - p.startAt <= 30 * 60 * 1000)
      target = &p;
  REQUIRE(target);
  INFO(target->name);

  RuleEdit e{target->name};
  e.channelId = channel;
  const int rule = api.AddRule(NewRuleBody(e));
  const auto reserves = WaitForReserves(api, [&](const Reserve& r) {
    return r.ruleId == rule && r.programId == target->id;
  });
  REQUIRE_FALSE(reserves.empty());
  const int id = reserves[0].id;
  CHECK(ToKodiTimer(reserves[0], now).type == timer_type::kRuleOnce);

  api.DeleteReserve(id); // Kodi: disable
  auto state = WaitForReserves(api, [&](const Reserve& r) { return r.id == id && r.isSkip; });
  REQUIRE(state.size() == 1);
  CHECK(ToKodiTimer(state[0], now).state == timer_state::kDisabled);

  api.UnskipReserve(id); // Kodi: enable
  state = WaitForReserves(api, [&](const Reserve& r) { return r.id == id && !r.isSkip; });
  REQUIRE(state.size() == 1);
  CHECK(ToKodiTimer(state[0], now).state == timer_state::kScheduled);

  api.DeleteRule(rule); // its reservations go with it
  std::this_thread::sleep_for(std::chrono::seconds(2));
  for (const auto& r : api.Reserves())
    CHECK(r.ruleId != rule);
}
