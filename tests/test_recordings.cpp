// Recordings against a real /api/recorded capture (tests/fixtures/prod/recorded.json).

#include "epgstation/Channels.h"
#include "epgstation/Recordings.h"

#include <algorithm>
#include <set>

#include "Fixtures.h"

#include <catch2/catch_test_macros.hpp>

using namespace epgstation;

namespace
{
Recording WithFiles(std::vector<VideoFile> files)
{
  Recording r;
  r.videoFiles = std::move(files);
  return r;
}
} // namespace

TEST_CASE("recordings: the production list maps to Kodi recordings")
{
  const RecordedPage page = ParseRecorded(ReadFile("prod/recorded.json"));
  REQUIRE(page.total == 83);
  REQUIRE(page.records.size() == 83);

  std::set<std::string> ids;
  int empty = 0;
  for (const auto& r : page.records)
  {
    CAPTURE(r.id, r.program.name);
    const KodiRecording k = ToKodiRecording(r, true);
    ids.insert(k.recordingId);
    CHECK_FALSE(k.title.empty());
    CHECK(k.duration > 0);
    CHECK(k.recordingTime > 1700000000);
    CHECK(k.channelUid != 0);
    CHECK(EpgsIdFromUid(k.channelUid) == r.program.channelId);
    CHECK(k.size >= 0);
    empty += k.size == 0 ? 1 : 0; // id 41: a failed 0-byte recording, still listed (deletable)
    CHECK_FALSE(k.inProgress);
    if (r.program.id)
      CHECK(k.epgEventId == BroadcastId(r.program.id));
  }
  CHECK(ids.size() == 83);
  CHECK(empty == 1);
}

TEST_CASE("recordings: the dev list (free channels, radio, an encoded file)")
{
  const RecordedPage page = ParseRecorded(ReadFile("dev/recorded.json"));
  REQUIRE(page.total == 6);
  REQUIRE(page.records.size() == 6);
  for (const auto& r : page.records)
  {
    CAPTURE(r.id, r.program.name);
    const KodiRecording k = ToKodiRecording(r, true);
    CHECK(k.recordingId == std::to_string(r.id));
    CHECK_FALSE(k.title.empty());
    CHECK(k.duration > 0);
    CHECK(EpgsIdFromUid(k.channelUid) == r.program.channelId);
    CHECK(k.size > 0);
    CHECK_FALSE(k.inProgress);
  }
  const Recording& hiyokko = *std::find_if(page.records.begin(), page.records.end(),
                                           [](const Recording& r) { return r.id == 6; });
  CHECK(PickVideoFile(hiyokko, true)->type == "encoded");
  CHECK(PickVideoFile(hiyokko, false)->type == "ts");
}

TEST_CASE("recordings: a known film")
{
  const RecordedPage page = ParseRecorded(ReadFile("prod/recorded.json"));
  const Recording& r = page.records.front();
  REQUIRE(r.id == 84);
  CHECK(r.ruleId == 2);
  const KodiRecording k = ToKodiRecording(r, false);
  CHECK(k.recordingId == "84");
  CHECK(k.title == "[映]マトリックス レザレクションズ(吹替版)[SS][字]");
  CHECK(k.duration == 9000);
  CHECK(k.recordingTime == 1791804600);
  CHECK(k.channelUid == KodiUid(4, 191));
  CHECK(k.thumbnailId == 80);
  CHECK(k.size == 22466087608LL); // > 4 GB: 64-bit
  CHECK(k.genreType == 0x10);
  CHECK(k.genreDescription == "映画（洋画）,映画（その他）");
  CHECK(k.plot.rfind(r.program.description + "\n\n◇番組名", 0) == 0);
}

TEST_CASE("recordings: which file Kodi plays")
{
  const VideoFile ts{1, "TS", "ts", 100};
  const VideoFile h264{2, "H.264", "encoded", 10};
  const VideoFile h265{3, "H.265", "encoded", 8};

  CHECK(PickVideoFile(WithFiles({ts}), true)->id == 1);
  CHECK(PickVideoFile(WithFiles({ts, h264}), true)->id == 2);
  CHECK(PickVideoFile(WithFiles({ts, h264}), false)->id == 1);
  CHECK(PickVideoFile(WithFiles({ts, h264, h265}), true)->id == 3); // newest encode
  CHECK(PickVideoFile(WithFiles({h264}), false)->id == 2); // TS deleted after encode
  CHECK(PickVideoFile(WithFiles({}), true) == nullptr);

  Recording r = WithFiles({ts, h264});
  CHECK(ToKodiRecording(r, true).size == 10);
  CHECK(ToKodiRecording(r, false).size == 100);
}

TEST_CASE("recordings: in progress, unknown channel and programme, bad JSON")
{
  const RecordedPage page = ParseRecorded(R"({"total":1,"records":[{"id":3,"startAt":1000000,
      "endAt":1300000,"name":"録画中","isRecording":true,"isEncoding":false,"isProtected":true,
      "videoFiles":[]}]})");
  const KodiRecording k = ToKodiRecording(page.records.at(0), true);
  CHECK(k.inProgress);
  CHECK(k.duration == 300);
  CHECK(k.channelUid == 0);
  CHECK(k.epgEventId == 0);
  CHECK(k.thumbnailId == -1);
  CHECK(k.size == 0);
  CHECK(page.records[0].ruleId == -1);
  CHECK(page.records[0].isProtected);

  const Recording one = ParseRecordedItem(R"({"id":8,"startAt":0,"endAt":1,"name":"x",
      "isRecording":false,"isEncoding":true,"isProtected":false,"ruleId":3,
      "videoFiles":[{"id":9,"name":"TS","type":"ts","size":5}]})");
  CHECK(one.id == 8);
  CHECK(one.isEncoding);
  CHECK(one.ruleId == 3);
  CHECK(one.videoFiles.at(0).id == 9);
  CHECK_THROWS(ParseRecordedItem("[]"));

  CHECK_THROWS(ParseRecorded("[]"));
  CHECK_THROWS(ParseRecorded(R"({"records":[{"id":1}]})"));
  CHECK(ParseRecorded(R"({"records":[],"total":0})").records.empty());
}
