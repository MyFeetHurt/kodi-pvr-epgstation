/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// EPGStation rules (/api/rules) <-> Kodi timer rules. No Kodi headers: unit-tested.

#include <cstdint>
#include <string>
#include <vector>

namespace epgstation
{

// Kodi timer ids of rules: reserve ids and rule ids overlap, Kodi needs one id space.
constexpr unsigned int kRuleIndexBase = 0x40000000;

struct Rule
{
  int id = 0;
  bool enabled = true;
  std::string keyword;
  // Only rules Kodi's timer dialog can show without losing anything are editable in Kodi: one
  // keyword (not a regular expression), title or full text, one channel or GR+BS+CS (no SKY),
  // at most one weekday/hour window, nothing else (genres, exclusions, durations...).
  bool editable = false;
  bool fullText = false; // keyword also searched in description/extended
  uint64_t channelId = 0; // 0 = any channel
  unsigned int kodiWeekdays = 0x7F; // PVR_WEEKDAY_* bits (Monday = 1)
  bool anyTime = true;
  int startHour = 0; // local hour, when !anyTime
  int endHour = 0;
  bool avoidDuplicate = false;
  std::string raw; // the rule JSON, kept so updates preserve what Kodi doesn't know
};

std::vector<Rule> ParseRules(const std::string& json); // GET /api/rules: {rules, total}

// EPGStation week bits (0x01 Sunday .. 0x40 Saturday) <-> Kodi (Monday = 1 .. Sunday = 64).
unsigned int KodiWeekdays(unsigned int epgsWeek);
unsigned int EpgsWeek(unsigned int kodiWeekdays); // 0 (none chosen) = every day

// What Kodi's dialog sets.
struct RuleEdit
{
  std::string keyword;
  bool fullText = false;
  uint64_t channelId = 0; // 0 = any
  unsigned int kodiWeekdays = 0x7F;
  bool anyTime = true;
  int startHour = 0;
  int endHour = 0;
  bool enabled = true;
  bool avoidDuplicate = false;
};

std::string NewRuleBody(const RuleEdit& edit); // POST /api/rules
// PUT /api/rules/{id}: `raw` with the fields Kodi controls replaced; all else kept.
std::string UpdatedRuleBody(const std::string& raw, const RuleEdit& edit);

// Series keyword from a programme title for "Record series": without leading ARIB markers
// [新] and 【...】 labels, cut before later markers [字] and episode parts (#5, 第3話, (48), 「subtitle」, ▽...).
// 【連続テレビ小説】ひよっこ(48)「夏の思い出はメロン色」[解][字] -> ひよっこ
std::string SeriesKeyword(const std::string& title);

} // namespace epgstation
