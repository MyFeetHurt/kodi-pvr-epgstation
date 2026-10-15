/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KodiHttp.h"

#include <cstdlib>

namespace
{
// Options every request gets: keep the body of HTTP errors (EPGStation puts the reason in it).
bool Create(kodi::vfs::CFile& file, const std::string& url)
{
  if (!file.CURLCreate(url))
    return false;
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "failonerror", "false");
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "user-agent",
                     "pvr.epgstation/" + kodi::addon::GetAddonInfo("version"));
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "connection-timeout", "10");
  return true;
}

// "HTTP/1.1 200 OK" -> 200; 0 when there was no response.
int Status(kodi::vfs::CFile& file)
{
  const std::string protocol = file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_PROTOCOL, "");
  const auto space = protocol.find(' ');
  return space == std::string::npos ? 0 : std::atoi(protocol.c_str() + space + 1);
}
} // namespace

epgstation::HttpResponse KodiHttp::Get(const std::string& url)
{
  return Request(url, nullptr);
}

epgstation::HttpResponse KodiHttp::Delete(const std::string& url)
{
  return Request(url, "DELETE");
}

epgstation::HttpResponse KodiHttp::Post(const std::string& url, const std::string& jsonBody)
{
  return Request(url, nullptr, &jsonBody);
}

epgstation::HttpResponse KodiHttp::Put(const std::string& url, const std::string& jsonBody)
{
  return Request(url, "PUT", &jsonBody); // customrequest overrides postdata's POST
}

epgstation::HttpResponse KodiHttp::Request(const std::string& url,
                                           const char* method,
                                           const std::string* jsonBody)
{
  epgstation::HttpResponse response;
  kodi::vfs::CFile file;
  if (!Create(file, url))
    return response;
  if (method)
    file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "customrequest", method);
  if (jsonBody)
  {
    // "postdata" (base64) makes it a POST.
    file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "postdata",
                       epgstation::Base64Encode(*jsonBody));
    file.CURLAddOption(ADDON_CURL_OPTION_HEADER, "Content-Type", "application/json");
  }
  if (!file.CURLOpen(ADDON_READ_NO_CACHE))
    return response;

  char buffer[16384];
  ssize_t n;
  while ((n = file.Read(buffer, sizeof(buffer))) > 0)
    response.body.append(buffer, static_cast<size_t>(n));
  response.status = Status(file);
  return response;
}

int KodiStreamFile::Open(const std::string& url)
{
  Close();
  m_file = std::make_unique<kodi::vfs::CFile>();
  if (!Create(*m_file, url))
    return 0;
  m_file->CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0"); // a plain GET, no Range
  if (!m_file->CURLOpen(ADDON_READ_NO_CACHE))
  {
    m_file.reset();
    return 0;
  }
  return Status(*m_file);
}

int64_t KodiStreamFile::Read(uint8_t* buffer, size_t size)
{
  return m_file ? m_file->Read(buffer, size) : -1;
}

void KodiStreamFile::Close()
{
  if (m_file)
  {
    m_file->Close();
    m_file.reset();
  }
}

KodiRangeFile::~KodiRangeFile()
{
  Close();
}

epgstation::IRangeSource::Opened KodiRangeFile::Open(int64_t offset)
{
  Close();
  Opened opened;
  m_file = std::make_unique<kodi::vfs::CFile>();
  if (!Create(*m_file, m_url))
    return opened;
  // Our own Range only: a seekable CCurlFile adds "Range: bytes=0-" to a request it starts at
  // position 0 (Kodi 22, and Kodi 21 with FFmpeg 7), EPGStation honours the first of the two and
  // every reopen at an offset read the file's start (seeks while recording landed at 0:00).
  m_file->CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "seekable", "0");
  m_file->CURLAddOption(ADDON_CURL_OPTION_HEADER, "Range", "bytes=" + std::to_string(offset) + "-");
  if (!m_file->CURLOpen(ADDON_READ_NO_CACHE))
  {
    m_file.reset();
    return opened;
  }
  opened.status = Status(*m_file);
  // "bytes 0-99/1000" or "bytes */1000"
  const std::string range = m_file->GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_HEADER,
                                                     "Content-Range");
  const auto slash = range.rfind('/');
  if (slash != std::string::npos && slash + 1 < range.size() && range[slash + 1] != '*')
    opened.total = std::atoll(range.c_str() + slash + 1);
  return opened;
}

int64_t KodiRangeFile::Read(uint8_t* buffer, size_t size)
{
  return m_file ? m_file->Read(buffer, size) : -1;
}

void KodiRangeFile::Close()
{
  if (m_file)
  {
    m_file->Close();
    m_file.reset();
  }
}
