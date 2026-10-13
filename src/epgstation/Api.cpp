/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Api.h"

#include <chrono>
#include <set>
#include <thread>

#include <nlohmann/json.hpp>

namespace epgstation
{

std::string Base64Encode(const std::string& data)
{
  static const char* const kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < data.size(); i += 3)
  {
    const unsigned int n = (static_cast<unsigned char>(data[i]) << 16) |
                           (static_cast<unsigned char>(data[i + 1]) << 8) |
                           static_cast<unsigned char>(data[i + 2]);
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
    out += kAlphabet[(n >> 6) & 63];
    out += kAlphabet[n & 63];
  }
  if (i < data.size())
  {
    unsigned int n = static_cast<unsigned char>(data[i]) << 16;
    if (i + 1 < data.size())
      n |= static_cast<unsigned char>(data[i + 1]) << 8;
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
    out += i + 1 < data.size() ? kAlphabet[(n >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

std::string NormalizeBaseUrl(std::string url)
{
  while (!url.empty() && url.back() == '/')
    url.pop_back();
  const std::string api = "/api";
  if (url.size() >= api.size() && url.compare(url.size() - api.size(), api.size(), api) == 0)
    url.resize(url.size() - api.size());
  return url;
}

Api::Api(IHttp& http, const std::string& baseUrl)
  : sleep([](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }),
    m_http(http),
    m_baseUrl(NormalizeBaseUrl(baseUrl))
{
}

std::string Api::GetJson(const std::string& path)
{
  return Call(Method::Get, path);
}

std::string Api::Call(Method method, const std::string& path, const std::string& body)
{
  const std::string url = m_baseUrl + "/api" + path;
  for (int attempt = 0;; ++attempt)
  {
    HttpResponse r = method == Method::Get      ? m_http.Get(url)
                     : method == Method::Delete ? m_http.Delete(url)
                     : method == Method::Post   ? m_http.Post(url, body)
                                                : m_http.Put(url, body);
    if (r.status == 200 || r.status == 201)
      return r.body;
    if (r.status == 0)
      throw ApiError(0, "unreachable: " + url);

    // EPGStation errors: {"code":500,"message":"...","errors":"<reason>"}
    std::string reason;
    const auto j = nlohmann::json::parse(r.body, nullptr, false);
    if (j.is_object() && j.contains("errors"))
      reason = j["errors"].is_string() ? j["errors"].get<std::string>() : j["errors"].dump();
    if (method == Method::Get && reason.find("SQLITE_BUSY") != std::string::npos && attempt < kRetries)
    {
      sleep(kRetryDelayMs);
      continue;
    }
    throw ApiError(r.status, "HTTP " + std::to_string(r.status) + " " + url +
                                 (reason.empty() ? "" : ": " + reason));
  }
}

std::string Api::Version()
{
  const auto j = nlohmann::json::parse(GetJson("/version"), nullptr, false);
  if (!j.is_object() || !j.contains("version") || !j["version"].is_string())
    throw ApiError(200, "version: unexpected response");
  return j["version"].get<std::string>();
}

std::vector<Channel> Api::Channels()
{
  return ParseChannels(GetJson("/channels"));
}

std::vector<Program> Api::ChannelSchedule(uint64_t channelId, int64_t startAtMs, int days)
{
  return ParseChannelSchedule(GetJson("/schedules/" + std::to_string(channelId) +
                                      "?startAt=" + std::to_string(startAtMs) +
                                      "&days=" + std::to_string(days) + "&isHalfWidth=true"));
}

ScheduleSnapshot Api::AllSchedules(int64_t fromMs, int64_t toMs)
{
  ScheduleSnapshot snap;
  snap.from = fromMs;
  snap.to = toMs;
  snap.programs = ParseChannelSchedule(GetJson("/schedules?startAt=" + std::to_string(fromMs) +
                                               "&endAt=" + std::to_string(toMs) +
                                               "&isHalfWidth=true&GR=true&BS=true&CS=true&SKY=true"));
  return snap;
}

std::string Api::ChannelLogoUrl(uint64_t channelId) const
{
  return m_baseUrl + "/api/channels/" + std::to_string(channelId) + "/logo";
}

LiveConfig Api::Config()
{
  return ParseLiveConfig(GetJson("/config"));
}

std::string Api::LiveStreamUrl(uint64_t channelId, int mode) const
{
  return m_baseUrl + "/api/streams/live/" + std::to_string(channelId) +
         "/m2ts?mode=" + std::to_string(mode);
}

void Api::AppendPaged(const std::string& path, std::vector<Recording>& out)
{
  size_t got = 0;
  for (int offset = 0;; offset += kRecordedPageSize)
  {
    RecordedPage page = ParseRecorded(GetJson(path + "?isHalfWidth=true&offset=" +
                                              std::to_string(offset) +
                                              "&limit=" + std::to_string(kRecordedPageSize)));
    const size_t n = page.records.size();
    got += n;
    for (auto& r : page.records)
      out.push_back(std::move(r));
    if (n < static_cast<size_t>(kRecordedPageSize) || got >= static_cast<size_t>(page.total))
      break;
  }
}

std::vector<Recording> Api::Recorded()
{
  std::vector<Recording> recording, recorded;
  AppendPaged("/recording", recording);
  AppendPaged("/recorded", recorded);
  // A recording that finished between the two requests is in both: keep the finished entry.
  std::set<int> finished;
  for (const auto& r : recorded)
    finished.insert(r.id);
  std::vector<Recording> all;
  for (auto& r : recording)
    if (!finished.count(r.id))
      all.push_back(std::move(r));
  for (auto& r : recorded)
    all.push_back(std::move(r));
  return all;
}

Recording Api::RecordedItem(int recordedId)
{
  return ParseRecordedItem(GetJson("/recorded/" + std::to_string(recordedId) + "?isHalfWidth=true"));
}

RecordedSignature Api::Signature()
{
  RecordedSignature s;
  const RecordedPage newest = ParseRecorded(GetJson("/recorded?isHalfWidth=true&limit=1"));
  s.total = newest.total;
  s.newestId = newest.records.empty() ? 0 : newest.records.front().id;
  s.recording = ParseRecorded(GetJson("/recording?isHalfWidth=true")).total;

  const auto encode = nlohmann::json::parse(GetJson("/encode?isHalfWidth=true"), nullptr, false);
  if (!encode.is_object())
    throw ApiError(200, "encode: unexpected response");
  s.encoding = static_cast<int>(encode.value("runningItems", nlohmann::json::array()).size() +
                                encode.value("waitItems", nlohmann::json::array()).size());
  return s;
}

void Api::DeleteRecorded(int recordedId)
{
  Call(Method::Delete, "/recorded/" + std::to_string(recordedId));
}

std::string Api::VideoUrl(int videoFileId) const
{
  return m_baseUrl + "/api/videos/" + std::to_string(videoFileId);
}

std::string Api::ThumbnailUrl(int thumbnailId) const
{
  return m_baseUrl + "/api/thumbnails/" + std::to_string(thumbnailId);
}

std::vector<Reserve> Api::Reserves()
{
  std::vector<Reserve> all;
  for (int offset = 0;; offset += kRecordedPageSize)
  {
    const std::string body = GetJson("/reserves?isHalfWidth=true&type=all&offset=" +
                                     std::to_string(offset) +
                                     "&limit=" + std::to_string(kRecordedPageSize));
    std::vector<Reserve> page = ParseReserves(body);
    const auto doc = nlohmann::json::parse(body, nullptr, false);
    const size_t total = doc.is_object() ? doc.value("total", 0) : 0;
    const size_t n = page.size();
    for (auto& r : page)
      all.push_back(std::move(r));
    if (n < static_cast<size_t>(kRecordedPageSize) || all.size() >= total)
      break;
  }
  return all;
}

std::string Api::ReservesFingerprint(time_t now)
{
  // From a week back: reservations recording now started in the past and must count too
  // (deleting one would otherwise go unnoticed).
  const int64_t ms = static_cast<int64_t>(now) * 1000;
  return GetJson("/reserves/lists?startAt=" + std::to_string(ms - 7LL * 86400 * 1000) +
                 "&endAt=" + std::to_string(ms + 60LL * 86400 * 1000));
}

int Api::AddReserve(const std::string& jsonBody)
{
  const auto j = nlohmann::json::parse(Call(Method::Post, "/reserves", jsonBody), nullptr, false);
  if (!j.is_object() || !j.contains("reserveId") || !j["reserveId"].is_number_integer())
    throw ApiError(200, "reserves: no reserveId in the answer");
  return j["reserveId"].get<int>();
}

void Api::DeleteReserve(int reserveId)
{
  Call(Method::Delete, "/reserves/" + std::to_string(reserveId));
}

void Api::UnskipReserve(int reserveId)
{
  Call(Method::Delete, "/reserves/" + std::to_string(reserveId) + "/skip");
}

std::vector<Rule> Api::Rules()
{
  std::vector<Rule> all;
  for (int offset = 0;; offset += kRecordedPageSize)
  {
    const std::string body = GetJson("/rules?offset=" + std::to_string(offset) +
                                     "&limit=" + std::to_string(kRecordedPageSize));
    std::vector<Rule> page = ParseRules(body);
    const auto doc = nlohmann::json::parse(body, nullptr, false);
    const size_t total = doc.is_object() ? doc.value("total", 0) : 0;
    const size_t n = page.size();
    for (auto& r : page)
      all.push_back(std::move(r));
    if (n < static_cast<size_t>(kRecordedPageSize) || all.size() >= total)
      break;
  }
  return all;
}

std::string Api::RulesFingerprint()
{
  return GetJson("/rules?offset=0&limit=" + std::to_string(kRecordedPageSize));
}

Rule Api::GetRule(int ruleId)
{
  const std::string body = GetJson("/rules/" + std::to_string(ruleId));
  std::vector<Rule> one = ParseRules("{\"rules\":[" + body + "]}");
  if (one.size() != 1)
    throw ApiError(200, "rule: unexpected response");
  return one.front();
}

int Api::AddRule(const std::string& jsonBody)
{
  const auto j = nlohmann::json::parse(Call(Method::Post, "/rules", jsonBody), nullptr, false);
  if (!j.is_object() || !j.contains("ruleId") || !j["ruleId"].is_number_integer())
    throw ApiError(200, "rules: no ruleId in the answer");
  return j["ruleId"].get<int>();
}

void Api::UpdateRule(int ruleId, const std::string& jsonBody)
{
  Call(Method::Put, "/rules/" + std::to_string(ruleId), jsonBody);
}

void Api::SetRuleEnabled(int ruleId, bool enabled)
{
  Call(Method::Put, "/rules/" + std::to_string(ruleId) + (enabled ? "/enable" : "/disable"), "{}");
}

void Api::DeleteRule(int ruleId)
{
  Call(Method::Delete, "/rules/" + std::to_string(ruleId));
}

} // namespace epgstation
