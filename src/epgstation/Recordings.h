/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// EPGStation recordings (/api/recorded) -> Kodi recordings. No Kodi headers: unit-tested.

#include "Programs.h"

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace epgstation
{

struct VideoFile
{
  int id = 0;
  std::string name; // "TS", or the encode mode, e.g. "H.264"
  std::string type; // "ts" or "encoded"
  int64_t size = 0;
};

// One recording (finished or in progress); the programme fields reuse Program.
struct Recording
{
  int id = 0;
  int ruleId = -1; // -1 = manual reservation
  Program program; // id = programId (0 when unknown), channelId, times, texts, genres
  bool isRecording = false;
  bool isEncoding = false;
  bool isProtected = false;
  std::vector<int> thumbnails;
  std::vector<VideoFile> videoFiles;
};

struct RecordedPage
{
  int total = 0;
  std::vector<Recording> records;
};

// GET /api/recorded answers {records: [...], total}. Throws std::runtime_error on anything else.
RecordedPage ParseRecorded(const std::string& json);
// GET /api/recorded/{id} answers one RecordedItem.
Recording ParseRecordedItem(const std::string& json);

// File Kodi plays: with preferEncoded the newest encoded file when there is one, else the TS
// (else whatever there is). nullptr when the recording has no file (yet).
const VideoFile* PickVideoFile(const Recording& recording, bool preferEncoded);

struct KodiRecording
{
  std::string recordingId; // EPGStation recorded id
  std::string title;
  std::string plotOutline;
  std::string plot;
  int genreType = 0;
  std::string genreDescription;
  time_t recordingTime = 0;
  int duration = 0; // seconds (scheduled; also while still recording)
  uint64_t channelId = 0; // EPGStation channel id; 0 = unknown
  unsigned int channelUid = 0; // 0 = unknown channel
  unsigned int epgEventId = 0; // 0 = no programme id
  int thumbnailId = -1;
  int64_t size = 0; // of the file Kodi plays
  bool inProgress = false;
};

KodiRecording ToKodiRecording(const Recording& recording, bool preferEncoded);

// Cheap fingerprint of the recordings list (total, newest id, in-progress / encoding state):
// when it changes the worker asks Kodi to reload its recordings.
struct RecordedSignature
{
  int total = -1; // finished (/api/recorded); in progress is `recording`
  int newestId = -1;
  int recording = -1;
  int encoding = -1;
  bool operator==(const RecordedSignature& o) const
  {
    return total == o.total && newestId == o.newestId && recording == o.recording &&
           encoding == o.encoding;
  }
  bool operator!=(const RecordedSignature& o) const { return !(*this == o); }
};

} // namespace epgstation
