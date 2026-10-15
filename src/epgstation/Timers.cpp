/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Timers.h"

#include "Channels.h"
#include "Programs.h"

#include <stdexcept>

#include "Json.h"

namespace epgstation
{

std::vector<Reserve> ParseReserves(const std::string& json)
{
  const auto doc = nlohmann::json::parse(json, nullptr, false);
  if (!doc.is_object() || !doc.contains("reserves") || !doc["reserves"].is_array())
    throw std::runtime_error("reserves: expected {reserves: [...]}");

  std::vector<Reserve> result;
  for (const auto& j : doc["reserves"])
  {
    Reserve r;
    r.id = j.at("id").get<int>();
    r.ruleId = json::NumberOr<int>(j, "ruleId", -1);
    r.programId = json::NumberOr<uint64_t>(j, "programId", 0);
    r.channelId = j.at("channelId").get<uint64_t>();
    r.startAt = j.at("startAt").get<int64_t>();
    r.endAt = j.at("endAt").get<int64_t>();
    r.name = json::StringOr(j, "name");
    r.description = json::StringOr(j, "description");
    r.isSkip = json::BoolOr(j, "isSkip", false);
    r.isConflict = json::BoolOr(j, "isConflict", false);
    r.isOverlap = json::BoolOr(j, "isOverlap", false);
    r.isTimeSpecified = json::BoolOr(j, "isTimeSpecified", false);
    result.push_back(std::move(r));
  }
  return result;
}

KodiTimer ToKodiTimer(const Reserve& reserve, time_t now)
{
  KodiTimer t;
  t.clientIndex = static_cast<unsigned int>(reserve.id);
  t.ruleId = reserve.ruleId;
  if (reserve.ruleId >= 0)
    t.type = timer_type::kRuleOnce;
  else if (reserve.isTimeSpecified)
    t.type = timer_type::kManualOnce;
  else
    t.type = timer_type::kEpgOnce;

  t.start = static_cast<time_t>(reserve.startAt / 1000);
  t.end = static_cast<time_t>(reserve.endAt / 1000);
  if (reserve.isSkip || reserve.isOverlap)
    t.state = timer_state::kDisabled;
  else if (reserve.isConflict)
    t.state = timer_state::kConflictNok;
  else if (now >= t.start && now < t.end)
    t.state = timer_state::kRecording;
  else
    t.state = timer_state::kScheduled;

  t.channelUid = KodiUidFromEpgsId(reserve.channelId);
  t.epgUid = reserve.programId > 0 ? BroadcastId(reserve.programId) : 0;
  t.title = reserve.name;
  t.summary = reserve.description;
  return t;
}

std::string EpgReserveBody(uint64_t programId)
{
  return nlohmann::json{{"programId", programId}, {"allowEndLack", true}}.dump();
}

std::string ManualReserveBody(const std::string& name,
                              uint64_t channelId,
                              time_t start,
                              time_t end)
{
  return nlohmann::json{{"timeSpecifiedOption",
                         {{"name", name},
                          {"channelId", channelId},
                          {"startAt", static_cast<int64_t>(start) * 1000},
                          {"endAt", static_cast<int64_t>(end) * 1000}}},
                        {"allowEndLack", true}}
      .dump();
}

} // namespace epgstation
