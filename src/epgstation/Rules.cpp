/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Rules.h"

#include <algorithm>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace epgstation
{

namespace
{
bool Flag(const nlohmann::json& j, const char* key)
{
  return j.contains(key) && j[key].is_boolean() && j[key].get<bool>();
}

bool NonEmptyArray(const nlohmann::json& j, const char* key)
{
  return j.contains(key) && j[key].is_array() && !j[key].empty();
}

bool Has(const nlohmann::json& j, const char* key)
{
  return j.contains(key) && !j[key].is_null() && !(j[key].is_string() && j[key].get<std::string>().empty());
}

nlohmann::json Times(const RuleEdit& e)
{
  nlohmann::json t = {{"week", EpgsWeek(e.kodiWeekdays)}};
  if (!e.anyTime)
  {
    int range = ((e.endHour - e.startHour) % 24 + 24) % 24;
    t["start"] = e.startHour;
    t["range"] = range == 0 ? 24 : range;
  }
  return nlohmann::json::array({t});
}

void Apply(nlohmann::json& rule, const RuleEdit& e)
{
  rule["isTimeSpecification"] = false;
  auto& s = rule["searchOption"];
  s["keyword"] = e.keyword;
  s["name"] = true;
  s["description"] = e.fullText;
  s["extended"] = e.fullText;
  if (e.channelId)
  {
    s["channelIds"] = nlohmann::json::array({e.channelId});
    s.erase("GR");
    s.erase("BS");
    s.erase("CS");
    s.erase("SKY");
  }
  else
  {
    s.erase("channelIds");
    s["GR"] = s["BS"] = s["CS"] = true;
    s["SKY"] = false;
  }
  s["times"] = Times(e);
  auto& r = rule["reserveOption"];
  r["enable"] = e.enabled;
  r["avoidDuplicate"] = e.avoidDuplicate;
  if (!r.contains("allowEndLack"))
    r["allowEndLack"] = true;
}

// Drops [open..close] spans at the start ("[新]", "【連続テレビ小説】"), repeatedly.
std::string DropLeadingSpans(std::string s, const std::string& open, const std::string& close)
{
  for (;;)
  {
    size_t lead = 0;
    while (lead < s.size() && s[lead] == ' ')
      ++lead;
    if (s.compare(lead, open.size(), open) != 0)
      return s;
    const auto b = s.find(close, lead + open.size());
    if (b == std::string::npos)
      return s;
    s.erase(0, b + close.size());
  }
}

std::string Trim(std::string s)
{
  static const std::string kSpaces[] = {" ", "　"};
  for (bool changed = true; changed;)
  {
    changed = false;
    for (const auto& sp : kSpaces)
    {
      while (s.size() >= sp.size() && s.compare(0, sp.size(), sp) == 0)
        s.erase(0, sp.size()), changed = true;
      while (s.size() >= sp.size() && s.compare(s.size() - sp.size(), sp.size(), sp) == 0)
        s.erase(s.size() - sp.size()), changed = true;
    }
  }
  return s;
}

size_t CodePoints(const std::string& s);

// "歴史探偵 選 竹中半兵衛と…" -> 歴史探偵: the series name is the first word of a title that
// goes on with an episode subtitle. A genre label first ("ドラマ 相棒") is skipped; a very
// short first word ("京都 嵯峨嵐山") keeps the next one too.
std::string FirstWords(const std::string& text)
{
  std::vector<std::string> words;
  std::string word;
  for (size_t i = 0; i < text.size();)
  {
    const bool ascii = text[i] == ' ';
    const bool wide = text.compare(i, 3, "\u3000") == 0;
    if (ascii || wide)
    {
      if (!word.empty())
        words.push_back(word), word.clear();
      i += ascii ? 1 : 3;
      continue;
    }
    word += text[i++];
  }
  if (!word.empty())
    words.push_back(word);
  if (words.size() < 2)
    return text;
  static const std::string kLabels[] = {"ドラマ", "アニメ", "映画", "韓ドラ", "韓国ドラマ",
                                        "中国ドラマ", "時代劇", "特集", "連続ドラマ", "劇場版"};
  for (const auto& label : kLabels)
    if (words[0] == label)
      return words[1];
  if (CodePoints(words[0]) <= 2 || words[0] == "THE" || words[0] == "The")
    return words[0] + " " + words[1];
  return words[0];
}

size_t CodePoints(const std::string& s)
{
  size_t n = 0;
  for (unsigned char c : s)
    n += (c & 0xC0) != 0x80 ? 1 : 0;
  return n;
}

bool IsDigitAt(const std::string& s, size_t i)
{
  if (i < s.size() && s[i] >= '0' && s[i] <= '9')
    return true;
  // full-width ０-９: EF BC 90 .. EF BC 99
  return i + 2 < s.size() && static_cast<unsigned char>(s[i]) == 0xEF &&
         static_cast<unsigned char>(s[i + 1]) == 0xBC &&
         static_cast<unsigned char>(s[i + 2]) >= 0x90 && static_cast<unsigned char>(s[i + 2]) <= 0x99;
}
} // namespace

std::vector<Rule> ParseRules(const std::string& json)
{
  const auto doc = nlohmann::json::parse(json, nullptr, false);
  if (!doc.is_object() || !doc.contains("rules") || !doc["rules"].is_array())
    throw std::runtime_error("rules: expected {rules: [...]}");

  std::vector<Rule> result;
  for (const auto& j : doc["rules"])
  {
    Rule r;
    r.id = j.at("id").get<int>();
    r.raw = j.dump();
    const auto s = j.value("searchOption", nlohmann::json::object());
    const auto o = j.value("reserveOption", nlohmann::json::object());
    r.enabled = o.value("enable", true);
    r.avoidDuplicate = o.value("avoidDuplicate", false);
    r.keyword = s.value("keyword", "");
    r.fullText = Flag(s, "description") || Flag(s, "extended");

    const auto channels = s.value("channelIds", nlohmann::json::array());
    if (channels.is_array() && channels.size() == 1)
      r.channelId = channels[0].get<uint64_t>();

    const auto times = s.value("times", nlohmann::json::array());
    if (times.is_array() && times.size() == 1)
    {
      r.kodiWeekdays = KodiWeekdays(times[0].value("week", 0x7F));
      if (times[0].contains("start") && times[0].contains("range"))
      {
        r.anyTime = false;
        r.startHour = times[0]["start"].get<int>();
        r.endHour = (r.startHour + times[0]["range"].get<int>()) % 24;
      }
    }

    // "Any channel" in Kodi is GR + BS + CS; a rule with SKY too would lose SKY on save.
    const bool allTypes = Flag(s, "GR") && Flag(s, "BS") && Flag(s, "CS") && !Flag(s, "SKY");
    r.editable = !j.value("isTimeSpecification", false) && !r.keyword.empty() &&
                 !Flag(s, "keyRegExp") && !Flag(s, "keyCS") && !Has(s, "ignoreKeyword") &&
                 !NonEmptyArray(s, "genres") && !Has(s, "durationMin") &&
                 !Has(s, "durationMax") && !NonEmptyArray(s, "searchPeriods") &&
                 !Flag(s, "isFree") && Flag(s, "name") &&
                 Flag(s, "description") == Flag(s, "extended") && times.size() <= 1 &&
                 (channels.size() == 1 || (channels.empty() && allTypes));
    result.push_back(std::move(r));
  }
  return result;
}

unsigned int KodiWeekdays(unsigned int epgsWeek)
{
  // EPGStation Sunday is bit 0, Kodi's is bit 6.
  return ((epgsWeek >> 1) | ((epgsWeek & 1) << 6)) & 0x7F;
}

unsigned int EpgsWeek(unsigned int kodiWeekdays)
{
  if ((kodiWeekdays & 0x7F) == 0)
    return 0x7F;
  return ((kodiWeekdays << 1) | ((kodiWeekdays >> 6) & 1)) & 0x7F;
}

std::string NewRuleBody(const RuleEdit& edit)
{
  nlohmann::json rule = {{"searchOption", nlohmann::json::object()},
                         {"reserveOption", nlohmann::json::object()}};
  Apply(rule, edit);
  return rule.dump();
}

std::string UpdatedRuleBody(const std::string& raw, const RuleEdit& edit)
{
  auto rule = nlohmann::json::parse(raw, nullptr, false);
  if (!rule.is_object())
    throw std::runtime_error("rule: bad stored JSON");
  rule.erase("id");
  Apply(rule, edit);
  return rule.dump();
}

namespace
{
std::string Keyword(const std::string& title, bool firstWords);
}

std::string SeriesKeyword(const std::string& title)
{
  return Keyword(title, true);
}

namespace
{
// firstWords: false for a name that follows a label marker ("映画「容疑者 室井慎次」"): that is
// the whole name, spaces and all.
std::string Keyword(const std::string& title, bool firstWords)
{
  std::string s = title;
  for (std::string before; before != s;)
  {
    before = s;
    s = DropLeadingSpans(s, "【", "】");
    s = DropLeadingSpans(s, "[", "]");
  }
  // Cut at the first episode / subtitle marker; a marker after the name ends it too
  // ("[映]ダーティハリー[吹]日曜洋画劇場" -> ダーティハリー).
  static const std::string kCuts[] = {"[", "【", "「", "」", "『", "』", "▽", "▼", "◇", "◆", "<",
                                      "＜", "〜", "～", "~", " #", "　#", "#", "＃", "第"};
  size_t cut = s.size();
  for (const auto& c : kCuts)
  {
    for (auto p = s.find(c); p != std::string::npos; p = s.find(c, p + 1))
    {
      // 第 only before a number (第3話); # only before one too.
      if ((c == "第" || c == "#" || c == "＃" || c == " #" || c == "　#") &&
          !IsDigitAt(s, p + c.size()))
        continue;
      if (p > 0) // never cut the whole title away
        cut = std::min(cut, p);
      break;
    }
  }
  // "(48)" / "（48）": an episode number in brackets.
  for (const std::string open : {"(", "（"})
  {
    for (auto p = s.find(open); p != std::string::npos && p < cut; p = s.find(open, p + 1))
    {
      if (p > 0 && IsDigitAt(s, p + open.size()))
      {
        cut = p;
        break;
      }
    }
  }
  const std::string head = Trim(s.substr(0, cut));
  // Only a label before the cut ("映画「容疑者 室井慎次」", "韓◆ワーキング・ガール"): the
  // name is what follows the marker.
  if (cut < s.size() && CodePoints(head) <= 2)
  {
    size_t next = cut + 1;
    while (next < s.size() && (static_cast<unsigned char>(s[next]) & 0xC0) == 0x80)
      ++next; // skip the rest of the marker's UTF-8 bytes
    const std::string named = Keyword(s.substr(next), false);
    if (CodePoints(named) > 2)
      return named;
  }
  const std::string result = firstWords ? FirstWords(head) : head;
  return result.empty() ? Trim(s) : result;
}
} // namespace

} // namespace epgstation
