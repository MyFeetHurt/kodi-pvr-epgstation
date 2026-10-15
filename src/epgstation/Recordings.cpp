/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Recordings.h"

#include "Channels.h"

#include <stdexcept>

#include "Json.h"

namespace epgstation
{

namespace
{
Recording ParseItem(const nlohmann::json& j)
{
  Recording r;
  r.id = j.at("id").get<int>();
  r.ruleId = json::NumberOr<int>(j, "ruleId", -1);
  Program& p = r.program;
  p.id = json::NumberOr<uint64_t>(j, "programId", 0);
  p.channelId = json::NumberOr<uint64_t>(j, "channelId", 0);
  p.startAt = j.at("startAt").get<int64_t>();
  p.endAt = j.at("endAt").get<int64_t>();
  p.name = j.at("name").get<std::string>();
  p.description = json::StringOr(j, "description");
  p.extended = json::StringOr(j, "extended");
  static const char* const genreKeys[3][2] = {
      {"genre1", "subGenre1"}, {"genre2", "subGenre2"}, {"genre3", "subGenre3"}};
  for (int i = 0; i < 3; ++i)
    p.genres[i] = {json::NumberOr<int>(j, genreKeys[i][0], -1), json::NumberOr<int>(j, genreKeys[i][1], -1)};
  r.isRecording = json::BoolOr(j, "isRecording", false);
  r.isEncoding = json::BoolOr(j, "isEncoding", false);
  r.isProtected = json::BoolOr(j, "isProtected", false);
  if (j.contains("thumbnails") && j["thumbnails"].is_array())
    for (const auto& t : j["thumbnails"])
      r.thumbnails.push_back(t.get<int>());
  if (j.contains("videoFiles") && j["videoFiles"].is_array())
  {
    for (const auto& v : j["videoFiles"])
      r.videoFiles.push_back({v.at("id").get<int>(), json::StringOr(v, "name"),
                              json::StringOr(v, "type"), json::NumberOr<int64_t>(v, "size", 0)});
  }
  return r;
}
} // namespace

Recording ParseRecordedItem(const std::string& json)
{
  const auto doc = nlohmann::json::parse(json, nullptr, false);
  if (!doc.is_object())
    throw std::runtime_error("recorded item: expected a JSON object");
  return ParseItem(doc);
}

RecordedPage ParseRecorded(const std::string& json)
{
  const auto doc = nlohmann::json::parse(json, nullptr, false);
  if (!doc.is_object() || !doc.contains("records") || !doc["records"].is_array())
    throw std::runtime_error("recorded: expected {records: [...]}");

  RecordedPage page;
  page.total = json::NumberOr<int>(doc, "total", 0);
  for (const auto& j : doc["records"])
    page.records.push_back(ParseItem(j));
  return page;
}

const VideoFile* PickVideoFile(const Recording& recording, bool preferEncoded)
{
  const VideoFile* ts = nullptr;
  const VideoFile* encoded = nullptr;
  for (const auto& v : recording.videoFiles)
  {
    if (v.type == "ts" && !ts)
      ts = &v;
    else if (v.type == "encoded")
      encoded = &v; // the last one is the newest
  }
  if (preferEncoded && encoded)
    return encoded;
  if (ts)
    return ts;
  if (encoded)
    return encoded;
  return recording.videoFiles.empty() ? nullptr : &recording.videoFiles.front();
}

KodiRecording ToKodiRecording(const Recording& recording, bool preferEncoded)
{
  const Program& p = recording.program;
  const KodiEpgTag tag = ToKodiEpgTag(p);

  KodiRecording k;
  k.recordingId = std::to_string(recording.id);
  k.title = tag.title;
  k.plotOutline = tag.plotOutline;
  k.plot = tag.plot;
  k.genreType = tag.genreType;
  k.genreDescription = tag.genreDescription;
  k.recordingTime = tag.start;
  k.duration = static_cast<int>(tag.end - tag.start);
  k.channelId = p.channelId;
  if (p.channelId > 0)
    k.channelUid = KodiUidFromEpgsId(p.channelId);
  k.epgEventId = p.id > 0 ? BroadcastId(p.id) : 0;
  k.thumbnailId = recording.thumbnails.empty() ? -1 : recording.thumbnails.front();
  if (const VideoFile* file = PickVideoFile(recording, preferEncoded))
    k.size = file->size;
  k.inProgress = recording.isRecording;
  return k;
}

} // namespace epgstation
