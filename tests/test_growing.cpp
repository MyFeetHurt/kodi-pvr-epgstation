// GrowingStream over a fake HTTP range source whose file grows while "recording".

#include "epgstation/GrowingStream.h"

#include <cstdio>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace epgstation;

namespace
{
// A file of `size` bytes (byte i = i % 251) served like EPGStation: 206 with Content-Range,
// 416 at/after the end. Tests grow it from the sleep callback (time passing).
class FakeFile : public IRangeSource
{
public:
  int64_t size = 0;
  bool emptyBodies = false; // 206 with a total beyond offset, but no body
  bool unreachable = false;
  std::vector<int64_t> opens;

  Opened Open(int64_t offset) override
  {
    opens.push_back(offset);
    if (unreachable)
      return {0, -1};
    m_pos = offset;
    m_end = size;
    if (offset >= size)
      return {416, size};
    return {206, size};
  }
  int64_t Read(uint8_t* buffer, size_t n) override
  {
    int64_t count = 0;
    if (emptyBodies)
      return 0;
    while (m_pos < m_end && count < static_cast<int64_t>(n))
      buffer[count++] = static_cast<uint8_t>(m_pos++ % 251);
    return count;
  }
  void Close() override {}

private:
  int64_t m_pos = 0;
  int64_t m_end = 0;
};

// Reads until Read() returns 0; checks every byte is the right one.
int64_t ReadAll(GrowingStream& s)
{
  uint8_t buf[64];
  int64_t total = 0;
  for (;;)
  {
    const int n = s.Read(buf, sizeof(buf));
    if (n <= 0)
      return total;
    for (int i = 0; i < n; ++i)
      REQUIRE(buf[i] == static_cast<uint8_t>((total + i) % 251));
    total += n;
  }
}
} // namespace

TEST_CASE("growing: a finished recording reads once to its end")
{
  FakeFile f;
  f.size = 1000;
  int checks = 0, slept = 0;
  GrowingStream s(f, [&] { ++checks; return false; }, [&](int ms) { slept += ms; });
  REQUIRE(s.Open());
  CHECK(ReadAll(s) == 1000);
  CHECK(s.Length() == 1000);
  CHECK(checks == 1);
  CHECK(slept == 0);
}

TEST_CASE("growing: follows the file while recording, stops when the recording ends")
{
  FakeFile f;
  f.size = 0; // recording just started: nothing written yet
  bool recording = true;
  int polls = 0;
  GrowingStream s(f, [&] { return recording; },
                  [&](int) {
                    f.size += 300; // the recorder writes while we wait
                    if (++polls == 20)
                      recording = false; // and then finishes
                  });
  REQUIRE(s.Open());
  const int64_t got = ReadAll(s);
  CHECK(got == f.size); // every byte the recorder wrote, in order
  CHECK(got > 300 * 5);
  CHECK(s.Length() == f.size);
}

TEST_CASE("growing: gives up after kMaxWaitMs when the recorder stalls")
{
  FakeFile f;
  f.size = 100; // recording, but the file never grows
  int slept = 0;
  GrowingStream s(f, [] { return true; }, [&](int ms) { slept += ms; });
  REQUIRE(s.Open());
  CHECK(ReadAll(s) == 100);
  CHECK(slept == GrowingStream::kMaxWaitMs);
}

TEST_CASE("growing: seek")
{
  FakeFile f;
  f.size = 1000;
  GrowingStream s(f, [] { return false; }, [](int) {});
  REQUIRE(s.Open());
  CHECK(s.Seek(500, SEEK_SET) == 500);
  CHECK(f.opens.back() == 500);
  uint8_t b;
  REQUIRE(s.Read(&b, 1) == 1);
  CHECK(b == 500 % 251);
  CHECK(s.Seek(-1, SEEK_END) == 999);
  CHECK(s.Seek(-999, SEEK_CUR) == 0);
  CHECK(s.Seek(-1, SEEK_CUR) == -1);
  CHECK(s.Seek(5000, SEEK_SET) == 1000); // clamped to the known end
  CHECK(s.Seek(-1, SEEK_SET) == -1);
  CHECK(s.Seek(0, 0x10) == 1); // SEEK_POSSIBLE
}

TEST_CASE("growing: an unreachable server ends playback instead of hanging")
{
  FakeFile f;
  f.size = 100;
  GrowingStream s(f, [] { return false; }, [](int) {});
  REQUIRE(s.Open());
  f.unreachable = true;
  uint8_t buf[200];
  CHECK(s.Read(buf, sizeof(buf)) == 100); // the open response still delivers
  CHECK(s.Read(buf, sizeof(buf)) == 0);
  CHECK(s.Seek(10, SEEK_SET) == -1);
}

TEST_CASE("growing: responses that promise data but deliver none end after kMaxWaitMs")
{
  FakeFile f;
  f.size = 1000;
  int slept = 0;
  GrowingStream s(f, [] { return true; }, [&](int ms) { slept += ms; });
  REQUIRE(s.Open());
  f.emptyBodies = true;
  uint8_t buf[16];
  CHECK(s.Read(buf, sizeof(buf)) == 0);
  CHECK(slept >= GrowingStream::kMaxWaitMs);
  CHECK(slept <= GrowingStream::kMaxWaitMs + 2 * GrowingStream::kPollMs);
}

TEST_CASE("growing: cancelling stops a read that waits for the recorder")
{
  FakeFile f;
  f.size = 100; // recording, but no new data
  bool cancel = false;
  int slept = 0;
  GrowingStream s(
      f, [] { return true; },
      [&](int ms) {
        slept += ms;
        if (slept >= 2000)
          cancel = true; // Kodi stops playback 2 s into the wait
      },
      [&] { return cancel; });
  REQUIRE(s.Open());
  uint8_t buf[200];
  CHECK(s.Read(buf, sizeof(buf)) == 100);
  CHECK(s.Read(buf, sizeof(buf)) == -1);
  CHECK(slept == 2000);
}

TEST_CASE("growing: cancelling during the empty-response backoff makes no further request")
{
  FakeFile f;
  f.size = 1000;
  bool cancel = false;
  GrowingStream s(f, [] { return true; }, [&](int) { cancel = true; }, [&] { return cancel; });
  REQUIRE(s.Open());
  f.emptyBodies = true;
  const size_t opens = f.opens.size();
  uint8_t buf[16];
  CHECK(s.Read(buf, sizeof(buf)) == -1);
  CHECK(f.opens.size() == opens + 1); // the one reopen that came back empty, nothing after
}
