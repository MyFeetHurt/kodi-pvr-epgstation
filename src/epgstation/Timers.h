/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// EPGStation reservations (/api/reserves) <-> Kodi timers. No Kodi headers: unit-tested.

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace epgstation
{

struct Reserve
{
  int id = 0;
  int ruleId = -1; // -1 = manual reservation
  uint64_t programId = 0; // 0 for time-specified reservations without a programme
  uint64_t channelId = 0;
  int64_t startAt = 0; // unix ms
  int64_t endAt = 0;
  std::string name;
  std::string description;
  bool isSkip = false; // a rule's reservation the user excluded
  bool isConflict = false; // no tuner
  bool isOverlap = false; // the same programme is already reserved
  bool isTimeSpecified = false; // manual by time, not by programme
};

// GET /api/reserves answers {reserves: [...], total}. Throws std::runtime_error otherwise.
std::vector<Reserve> ParseReserves(const std::string& json);

// Kodi timer type ids (GetTimerTypes). Rule-made reservations can't be created or edited here.
namespace timer_type
{
constexpr unsigned int kEpgOnce = 1; // by programme (from the guide)
constexpr unsigned int kManualOnce = 2; // by channel and time
constexpr unsigned int kRuleOnce = 3; // made by an EPGStation rule
constexpr unsigned int kRuleKeyword = 4; // rule: keyword (+ channel, weekdays, hours)
constexpr unsigned int kRuleSeries = 5; // rule from the guide ("Record series")
constexpr unsigned int kRuleReadOnly = 6; // rule Kodi cannot show fully: enable/disable/delete
} // namespace timer_type

// Kodi PVR_TIMER_STATE_* values (pvr_timers.h).
namespace timer_state
{
constexpr int kScheduled = 1;
constexpr int kRecording = 2;
constexpr int kConflictNok = 7;
constexpr int kDisabled = 9;
} // namespace timer_state

struct KodiTimer
{
  unsigned int clientIndex = 0; // reserve id
  unsigned int type = timer_type::kEpgOnce;
  int state = timer_state::kScheduled;
  unsigned int channelUid = 0;
  unsigned int epgUid = 0; // BroadcastId(programId); 0 = none
  std::string title;
  std::string summary;
  time_t start = 0;
  time_t end = 0;
  int ruleId = -1;
};

KodiTimer ToKodiTimer(const Reserve& reserve, time_t now);

// POST /api/reserves bodies.
std::string EpgReserveBody(uint64_t programId);
std::string ManualReserveBody(const std::string& name,
                              uint64_t channelId,
                              time_t start,
                              time_t end);

} // namespace epgstation
