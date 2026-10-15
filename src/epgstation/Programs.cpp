/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Programs.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>

#include "Json.h"

namespace epgstation
{

namespace
{
#include "GenreNames.inc"

// ARIB genre -> DVB content category (Kodi's EPG_EVENT_CONTENTMASK_* values).
constexpr int kKodiGenreType[16] = {
    0x20, // 0 ニュース・報道 -> news/current affairs
    0x40, // 1 スポーツ -> sports
    0x80, // 2 情報・ワイドショー -> social/political/economics (magazines)
    0x10, // 3 ドラマ -> movie/drama
    0x60, // 4 音楽 -> music/ballet/dance
    0x30, // 5 バラエティ -> show/game show
    0x10, // 6 映画 -> movie/drama
    0x50, // 7 アニメ・特撮 -> children's/youth (cartoons)
    0x90, // 8 ドキュメンタリー・教養 -> education/science/factual
    0x70, // 9 劇場・公演 -> arts/culture
    0xA0, // 10 趣味・教育 -> leisure hobbies
    0x80, // 11 福祉 -> social
    0, 0, 0, // 12-13 予備, 14 拡張: no colour
    0xF0, // 15 その他 -> user defined
};

} // namespace

std::vector<Program> ParseChannelSchedule(const std::string& json)
{
  const auto doc = nlohmann::json::parse(json, nullptr, false);
  if (!doc.is_array())
    throw std::runtime_error("schedule: expected a JSON array");

  std::vector<Program> result;
  for (const auto& channel : doc)
  {
    if (!channel.contains("programs") || !channel["programs"].is_array())
      throw std::runtime_error("schedule: entry without programs");
    for (const auto& j : channel["programs"])
    {
      Program p;
      p.id = j.at("id").get<uint64_t>();
      p.channelId = j.at("channelId").get<uint64_t>();
      p.startAt = j.at("startAt").get<int64_t>();
      p.endAt = j.at("endAt").get<int64_t>();
      p.name = j.at("name").get<std::string>();
      p.description = json::StringOr(j, "description");
      p.extended = json::StringOr(j, "extended");
      static const char* const genreKeys[3][2] = {
          {"genre1", "subGenre1"}, {"genre2", "subGenre2"}, {"genre3", "subGenre3"}};
      for (int i = 0; i < 3; ++i)
        p.genres[i] = {json::NumberOr<int>(j, genreKeys[i][0], -1), json::NumberOr<int>(j, genreKeys[i][1], -1)};
      result.push_back(std::move(p));
    }
  }
  return result;
}

std::string GenreName(const Genre& genre)
{
  if (genre.genre < 0 || genre.genre > 15)
    return "";
  std::string name = kGenreJa[genre.genre];
  if (genre.sub >= 0 && genre.sub <= 15 && kSubGenreJa[genre.genre][genre.sub])
    name += std::string("（") + kSubGenreJa[genre.genre][genre.sub] + "）";
  return name;
}

std::string GenreDescription(const Program& program)
{
  std::vector<std::string> names;
  for (const auto& g : program.genres)
  {
    const std::string name = GenreName(g);
    if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
      names.push_back(name);
  }
  std::string joined;
  for (const auto& n : names)
    joined += (joined.empty() ? "" : ",") + n;
  return joined;
}

int KodiGenreType(int aribGenre)
{
  return aribGenre >= 0 && aribGenre <= 15 ? kKodiGenreType[aribGenre] : 0;
}

unsigned int TitleFlags(const std::string& name)
{
  unsigned int result = 0;
  if (name.find("[新]") != std::string::npos)
    result |= flags::kNew;
  if (name.find("[初]") != std::string::npos)
    result |= flags::kPremiere;
  if (name.find("[終]") != std::string::npos)
    result |= flags::kFinale;
  if (name.find("[生]") != std::string::npos)
    result |= flags::kLive;
  return result;
}

unsigned int BroadcastId(uint64_t programId)
{
  return 0x10000u | static_cast<unsigned int>(programId % 100000);
}

uint64_t ProgramId(unsigned int channelUid, unsigned int broadcastId)
{
  const uint64_t networkId = channelUid >> 16;
  const uint64_t serviceId = channelUid & 0xFFFF;
  return networkId * 10000000000ULL + serviceId * 100000ULL + (broadcastId & 0xFFFF);
}

KodiEpgTag ToKodiEpgTag(const Program& program)
{
  KodiEpgTag tag;
  tag.broadcastId = BroadcastId(program.id);
  tag.start = static_cast<time_t>(program.startAt / 1000);
  tag.end = static_cast<time_t>(program.endAt / 1000);
  tag.title = program.name;
  tag.plotOutline = program.description;
  tag.plot = program.description;
  if (!program.extended.empty())
    tag.plot += (tag.plot.empty() ? "" : "\n\n") + program.extended;
  // Colour by the first genre present: some CS channels leave genre1 empty and put the
  // genre in genre2 (e.g. ザ・シネマ films: genre2 = 映画 / 洋画).
  for (const auto& g : program.genres)
  {
    if (g.genre >= 0)
    {
      tag.genreType = KodiGenreType(g.genre);
      break;
    }
  }
  tag.genreDescription = GenreDescription(program);
  tag.flags = TitleFlags(program.name);
  return tag;
}

std::vector<KodiEpgTag> ToKodiEpg(const std::vector<Program>& programs, time_t start, time_t end)
{
  std::vector<KodiEpgTag> tags;
  std::set<uint64_t> seen; // multi-day schedules repeat programmes that cross a 24 h slice
  for (const auto& p : programs)
  {
    if (!seen.insert(p.id).second)
      continue;
    KodiEpgTag tag = ToKodiEpgTag(p);
    if (tag.end > start && tag.start < end)
      tags.push_back(std::move(tag));
  }
  std::sort(tags.begin(), tags.end(),
            [](const KodiEpgTag& a, const KodiEpgTag& b) { return a.start < b.start; });
  return tags;
}

std::vector<uint64_t> ChangedChannels(const ScheduleSnapshot& before, const ScheduleSnapshot& after)
{
  const int64_t from = std::max(before.from, after.from);
  const int64_t to = std::min(before.to, after.to);
  if (from >= to)
    return {};

  // channel -> programme id -> everything Kodi shows of it (ToKodiEpgTag), for programmes
  // overlapping [from, to)
  using Shown = std::tuple<int64_t, int64_t, std::string, std::string, std::string, std::string>;
  using Programmes = std::map<uint64_t, std::map<uint64_t, Shown>>;
  auto index = [&](const ScheduleSnapshot& snap) {
    Programmes byChannel;
    for (const auto& p : snap.programs)
    {
      if (p.endAt <= from || p.startAt >= to)
        continue;
      std::string genres;
      for (const auto& g : p.genres)
        genres += std::to_string(g.genre) + "." + std::to_string(g.sub) + ";";
      byChannel[p.channelId][p.id] =
          Shown(p.startAt, p.endAt, p.name, p.description, p.extended, genres);
    }
    return byChannel;
  };
  const Programmes a = index(before);
  const Programmes b = index(after);

  std::set<uint64_t> channels;
  for (const auto& [channel, programmes] : a)
  {
    const auto it = b.find(channel);
    if (it == b.end() || it->second != programmes)
      channels.insert(channel);
  }
  for (const auto& [channel, programmes] : b)
    if (!a.count(channel))
      channels.insert(channel);
  return {channels.begin(), channels.end()};
}

int ScheduleDays(time_t start, time_t end)
{
  if (end <= start)
    return 1;
  return static_cast<int>((end - start + 86399) / 86400);
}

} // namespace epgstation
