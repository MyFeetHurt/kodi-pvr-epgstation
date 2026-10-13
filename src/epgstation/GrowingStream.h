/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// Reads a recording's file over HTTP ranges while EPGStation may still be writing it
// (追っかけ再生). EPGStation answers each request with the file size at that moment, so a
// plain stream URL ends where the file was when playback started. No Kodi headers: tested.

#include <cstddef>
#include <cstdint>
#include <functional>

namespace epgstation
{

// One HTTP range request at a time ("Range: bytes=offset-").
class IRangeSource
{
public:
  virtual ~IRangeSource() = default;
  // HTTP status (206/200 data, 416 nothing at offset yet, 0 unreachable) and the file's total
  // size from Content-Range ("bytes a-b/total" or "bytes */total"), -1 when unknown.
  struct Opened
  {
    int status = 0;
    int64_t total = -1;
  };
  virtual Opened Open(int64_t offset) = 0;
  virtual int64_t Read(uint8_t* buffer, size_t size) = 0; // > 0 bytes, 0 end, < 0 error
  virtual void Close() = 0;
};

class GrowingStream
{
public:
  // stillRecording: asks EPGStation (GET /api/recorded/{id}); called at most every
  // kRecheckMs while playback has caught up with the recorder.
  // cancelled: polled before every request and wait; true makes Read() return -1 at once
  // (playback is being stopped while we wait for the recorder).
  GrowingStream(IRangeSource& source,
                std::function<bool()> stillRecording,
                std::function<void(int ms)> sleep,
                std::function<bool()> cancelled = [] { return false; });

  bool Open(); // from the start
  void Close();
  // Blocks while caught up with a recording in progress (polling every kPollMs, at most
  // kMaxWaitMs per call); 0 only at the real end or after kMaxWaitMs without new data;
  // -1 when cancelled.
  int Read(uint8_t* buffer, size_t size);
  int64_t Seek(int64_t position, int whence); // SEEK_SET/CUR/END; -1 on error
  int64_t Length() const { return m_length; } // last known size (grows while recording)
  int64_t Position() const { return m_position; }

  static constexpr int kPollMs = 500;
  static constexpr int kRecheckMs = 5000;
  static constexpr int kMaxWaitMs = 20000;

private:
  bool OpenAt(int64_t offset); // false: unreachable / HTTP error
  bool Recording(); // stillRecording(), cached for kRecheckMs of sleeping

  IRangeSource& m_source;
  std::function<bool()> m_stillRecording;
  std::function<void(int)> m_sleep;
  std::function<bool()> m_cancelled;
  int64_t m_position = 0;
  int64_t m_length = 0;
  bool m_open = false; // a request is open and has data at m_position
  bool m_recording = true;
  int m_sinceRecheckMs = kRecheckMs; // forces the first check
};

} // namespace epgstation
