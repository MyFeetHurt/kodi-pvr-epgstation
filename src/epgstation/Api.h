/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// EPGStation REST client. HTTP is injected (IHttp) so tests run without Kodi.

#include "Channels.h"
#include "Live.h"
#include "Programs.h"
#include "Recordings.h"
#include "Rules.h"
#include "Timers.h"

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace epgstation
{

struct HttpResponse
{
  int status = 0; // 0 = no response (connection failed)
  std::string body;
};

// Disk space of EPGStation's recording folders, in bytes. Folders on one disk are counted once.
struct Storage
{
  uint64_t total = 0;
  uint64_t used = 0;
};
Storage ParseStorages(const std::string& json); // throws std::runtime_error on bad JSON

class IHttp
{
public:
  virtual ~IHttp() = default;
  virtual HttpResponse Get(const std::string& url) = 0;
  virtual HttpResponse Delete(const std::string& url) = 0;
  virtual HttpResponse Post(const std::string& url, const std::string& jsonBody) = 0;
  virtual HttpResponse Put(const std::string& url, const std::string& jsonBody) = 0;
};

class ApiError : public std::runtime_error
{
public:
  ApiError(int status, const std::string& what) : std::runtime_error(what), m_status(status) {}
  int Status() const { return m_status; } // 0 = unreachable
private:
  int m_status;
};

class Api
{
public:
  // baseUrl: "http://host:8888" (a trailing "/" or "/api" is accepted).
  Api(IHttp& http, const std::string& baseUrl);

  const std::string& BaseUrl() const { return m_baseUrl; }

  std::string Version(); // GET /api/version
  std::vector<Channel> Channels(); // GET /api/channels
  // GET /api/schedules/{channelId}?startAt=..&days=..&isHalfWidth=true
  std::vector<Program> ChannelSchedule(uint64_t channelId, int64_t startAtMs, int days);
  std::string ChannelLogoUrl(uint64_t channelId) const;
  // GET /api/schedules?startAt&endAt for all broadcast types: every channel's programmes.
  ScheduleSnapshot AllSchedules(int64_t fromMs, int64_t toMs);
  LiveConfig Config(); // GET /api/config
  // MPEG-TS of the channel, as long as the connection stays open: EPGStation tunes on the first
  // request and frees the tuner when the client disconnects.
  std::string LiveStreamUrl(uint64_t channelId, int mode) const;

  // All recordings: in progress (GET /api/recording) then finished (GET /api/recorded, newest
  // first), each in pages. /api/recorded leaves out recordings still in progress.
  std::vector<Recording> Recorded();
  Recording RecordedItem(int recordedId); // GET /api/recorded/{id}
  // GET /api/recorded?limit=1, /api/recording, /api/encode: see RecordedSignature.
  RecordedSignature Signature();
  // DELETE /api/recorded/{id}: the recording and its files. Not retried.
  void DeleteRecorded(int recordedId);
  std::string VideoUrl(int videoFileId) const; // the file, with HTTP range support
  std::string ThumbnailUrl(int thumbnailId) const;
  // GET /api/storages: the recording folders' disks, in bytes.
  Storage Storages();

  // All reservations (GET /api/reserves?type=all), in pages.
  std::vector<Reserve> Reserves();
  // GET /api/reserves/lists from 7 days back to 60 days ahead: ids, programme ids and state per reservation,
  // small enough to poll; changes when reservations are added, removed, skipped or conflict.
  std::string ReservesFingerprint(time_t now);
  // POST /api/reserves; returns the new reserve id. Not retried (it could reserve twice).
  int AddReserve(const std::string& jsonBody);
  void DeleteReserve(int reserveId); // DELETE /api/reserves/{id} (a rule's: skips it)
  void UnskipReserve(int reserveId); // DELETE /api/reserves/{id}/skip

  std::vector<Rule> Rules(); // GET /api/rules, in pages
  std::string RulesFingerprint(); // the raw first page of /api/rules, for change detection
  Rule GetRule(int ruleId); // GET /api/rules/{id}
  int AddRule(const std::string& jsonBody); // POST /api/rules -> rule id. Not retried.
  void UpdateRule(int ruleId, const std::string& jsonBody); // PUT /api/rules/{id}
  void SetRuleEnabled(int ruleId, bool enabled); // PUT /api/rules/{id}/enable|disable
  void DeleteRule(int ruleId); // DELETE /api/rules/{id} (its reservations go too)

  static constexpr int kPageSize = 100; // recordings, reservations, rules

  // EPGStation answers HTTP 500 {"errors":"SQLITE_BUSY..."} while its DB is locked; such GETs
  // are retried kRetries times, kRetryDelayMs apart. Tests replace the sleep.
  static constexpr int kRetries = 3;
  static constexpr int kRetryDelayMs = 1000;
  std::function<void(int ms)> sleep;

private:
  std::string GetJson(const std::string& path);
  // Every page (offset/limit) of a {<items>, total} list; parse gives one page's items.
  template<typename T>
  std::vector<T> Paged(const std::string& pathAndQuery,
                       const std::function<std::vector<T>(const std::string&)>& parse);
  // Status check + EPGStation's error reason; SQLITE_BUSY retried only for idempotent calls.
  enum class Method
  {
    Get,
    Delete,
    Post,
    Put
  };
  std::string Call(Method method, const std::string& path, const std::string& body = "");

  IHttp& m_http;
  std::string m_baseUrl;
};

// Kodi's curl takes a POST body as base64 ("postdata" option).
std::string Base64Encode(const std::string& data);

// "http://host:8888/api/" -> "http://host:8888"
std::string NormalizeBaseUrl(std::string url);

} // namespace epgstation
