/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KodiHttp.h"

#include <cstdlib>


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
  if (!file.CURLCreate(url))
    return response;
  // Keep the body of HTTP errors: EPGStation puts the reason in it.
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "failonerror", "false");
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "user-agent",
                     "pvr.epgstation/" + kodi::addon::GetAddonInfo("version"));
  file.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "connection-timeout", "10");
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

  // "HTTP/1.1 200 OK"
  const std::string protocol = file.GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_PROTOCOL, "");
  const auto space = protocol.find(' ');
  response.status = space == std::string::npos ? 0 : std::atoi(protocol.c_str() + space + 1);
  return response;
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
  if (!m_file->CURLCreate(m_url))
    return opened;
  m_file->CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "failonerror", "false");
  m_file->CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "user-agent",
                        "pvr.epgstation/" + kodi::addon::GetAddonInfo("version"));
  m_file->CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "connection-timeout", "10");
  m_file->CURLAddOption(ADDON_CURL_OPTION_HEADER, "Range", "bytes=" + std::to_string(offset) + "-");
  if (!m_file->CURLOpen(ADDON_READ_NO_CACHE))
  {
    m_file.reset();
    return opened;
  }
  const std::string protocol = m_file->GetPropertyValue(ADDON_FILE_PROPERTY_RESPONSE_PROTOCOL, "");
  const auto space = protocol.find(' ');
  opened.status = space == std::string::npos ? 0 : std::atoi(protocol.c_str() + space + 1);
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
