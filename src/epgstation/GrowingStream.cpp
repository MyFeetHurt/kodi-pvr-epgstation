/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "GrowingStream.h"

#include <algorithm>
#include <cstdio>

namespace epgstation
{

GrowingStream::GrowingStream(IRangeSource& source,
                             std::function<bool()> stillRecording,
                             std::function<void(int ms)> sleep,
                             std::function<bool()> cancelled)
  : m_source(source),
    m_stillRecording(std::move(stillRecording)),
    m_sleep(std::move(sleep)),
    m_cancelled(std::move(cancelled))
{
}

bool GrowingStream::OpenAt(int64_t offset)
{
  m_source.Close();
  m_open = false;
  const IRangeSource::Opened o = m_source.Open(offset);
  if (o.total >= 0)
    m_length = std::max(m_length, o.total);
  if (o.status == 206 || o.status == 200)
  {
    // 200: the server ignored the range; only usable from the start.
    if (o.status == 200 && offset != 0)
      return false;
    m_open = true;
    return true;
  }
  return o.status == 416; // nothing at offset yet: not an error while recording
}

bool GrowingStream::Open()
{
  m_position = 0;
  m_length = 0;
  m_recording = true;
  m_sinceRecheckMs = kRecheckMs;
  return OpenAt(0);
}

void GrowingStream::Close()
{
  m_source.Close();
  m_open = false;
}

bool GrowingStream::Recording()
{
  if (m_recording && m_sinceRecheckMs >= kRecheckMs)
  {
    m_recording = m_stillRecording();
    m_sinceRecheckMs = 0;
  }
  return m_recording;
}

int GrowingStream::Read(uint8_t* buffer, size_t size)
{
  int waitedMs = 0;
  bool reopened = false; // m_open comes from a request made in this call
  for (;;)
  {
    if (m_cancelled())
      return -1;
    if (m_open)
    {
      const int64_t n = m_source.Read(buffer, size);
      if (n > 0)
      {
        m_position += n;
        m_length = std::max(m_length, m_position);
        return static_cast<int>(n);
      }
      m_open = false; // end of this response (or an error): ask again from m_position
      if (reopened)
      {
        // A fresh response that promised data delivered none: back off like any wait, so a
        // misbehaving server cannot keep us retrying forever.
        if (waitedMs >= kMaxWaitMs)
          return 0;
        if (m_cancelled())
          return -1;
        m_sleep(kPollMs);
        waitedMs += kPollMs;
        m_sinceRecheckMs += kPollMs;
      }
    }
    reopened = false;
    if (m_cancelled()) // a backoff above may have slept: no more requests once stopping
      return -1;

    // Caught up. More may have been written meanwhile: one new request first.
    if (!OpenAt(m_position))
    {
      if (!Recording() || waitedMs >= kMaxWaitMs)
        return 0;
    }
    else if (m_open && m_length > m_position)
    {
      reopened = true;
      continue;
    }
    else if (!Recording() || waitedMs >= kMaxWaitMs)
    {
      return 0; // finished (or the recorder stalled): the real end
    }
    if (m_cancelled())
      return -1;
    m_sleep(kPollMs);
    waitedMs += kPollMs;
    m_sinceRecheckMs += kPollMs;
  }
}

int64_t GrowingStream::Seek(int64_t position, int whence)
{
  int64_t target;
  switch (whence)
  {
    case SEEK_SET:
      target = position;
      break;
    case SEEK_CUR:
      target = m_position + position;
      break;
    case SEEK_END:
      target = m_length + position;
      break;
    case 0x10: // Kodi's SEEK_POSSIBLE: "can you seek?"
      return 1;
    default:
      return -1;
  }
  if (target < 0)
    return -1;
  target = std::min(target, m_length);
  if (!OpenAt(target))
    return -1;
  m_position = target;
  return m_position;
}

} // namespace epgstation
