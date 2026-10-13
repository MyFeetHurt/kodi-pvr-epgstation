/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// Pure EPGStation programme -> Kodi EPG tag mapping. No Kodi headers: unit-tested on its own.

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace epgstation
{

struct Genre
{
  int genre = -1; // ARIB content_nibble_level_1 (0-15), -1 = none
  int sub = -1; // content_nibble_level_2
};

// One programme from /api/schedules (only the fields we use).
struct Program
{
  uint64_t id = 0; // networkId * 10^10 + serviceId * 10^5 + eventId
  uint64_t channelId = 0;
  int64_t startAt = 0; // unix ms
  int64_t endAt = 0;
  std::string name;
  std::string description;
  std::string extended;
  Genre genres[3];
};

// GET /api/schedules/{channelId} answers [{channel, programs: [...]}]. Throws std::runtime_error
// on anything else.
std::vector<Program> ParseChannelSchedule(const std::string& json);

// "映画（洋画）"; "" when the genre is missing or unknown. Not "映画 / 洋画": Kodi joins several
// genres with " / ", which would make two genres read as four.
std::string GenreName(const Genre& genre);
// All genre names of a programme, de-duplicated, joined with "," (Kodi's
// EPG_STRING_TOKEN_SEPARATOR, which none of the names contains).
std::string GenreDescription(const Program& program);
// DVB content category (EPG_EVENT_CONTENTMASK_*) closest to an ARIB genre: drives Kodi's guide
// colours. 0 (undefined) when unknown.
int KodiGenreType(int aribGenre);

// Kodi EPG_TAG_FLAG_* bits from the ARIB title markers EPGStation keeps in the name.
namespace flags
{
constexpr unsigned int kNew = 1 << 1; // [新]
constexpr unsigned int kPremiere = 1 << 2; // [初]
constexpr unsigned int kFinale = 1 << 3; // [終]
constexpr unsigned int kLive = 1 << 4; // [生]
} // namespace flags
unsigned int TitleFlags(const std::string& name);

// Kodi broadcast ids are 32-bit and must be > 0 and unique per channel: 0x10000 | eventId.
unsigned int BroadcastId(uint64_t programId);
uint64_t ProgramId(unsigned int channelUid, unsigned int broadcastId);

struct KodiEpgTag
{
  unsigned int broadcastId = 0;
  time_t start = 0;
  time_t end = 0;
  std::string title;
  std::string plotOutline; // description
  std::string plot; // description + extended
  int genreType = 0; // EPG_EVENT_CONTENTMASK_*; the sub type is always EPG_GENRE_USE_STRING
  std::string genreDescription;
  unsigned int flags = 0;
};

KodiEpgTag ToKodiEpgTag(const Program& program);

// Programmes overlapping [start, end) (unix seconds), in start order, each programme once
// (EPGStation returns one block per 24 h slice and repeats programmes crossing a slice).
std::vector<KodiEpgTag> ToKodiEpg(const std::vector<Program>& programs, time_t start, time_t end);

// "days" for GET /api/schedules/{channelId}?startAt=start covering up to end (at least 1).
int ScheduleDays(time_t start, time_t end);

// The near-future guide of all channels (GET /api/schedules?startAt&endAt), to notice EPG
// changes (overruns, EPGStation's own EPG updates) between Kodi's own EPG refreshes.
struct ScheduleSnapshot
{
  int64_t from = 0; // unix ms
  int64_t to = 0;
  std::vector<Program> programs;
};

// Channels (EPGStation ids) whose programmes differ between two snapshots, compared only where
// both windows overlap (the window moves on, programmes entering it are not a change).
std::vector<uint64_t> ChangedChannels(const ScheduleSnapshot& before, const ScheduleSnapshot& after);

} // namespace epgstation
