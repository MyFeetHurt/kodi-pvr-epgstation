/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "epgstation/Api.h"
#include "epgstation/GrowingStream.h"

#include <memory>

#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>

// IHttp over Kodi's own curl (kodi::vfs::CFile): no bundled HTTP library.
class ATTR_DLL_LOCAL KodiHttp : public epgstation::IHttp
{
public:
  epgstation::HttpResponse Get(const std::string& url) override;
  epgstation::HttpResponse Delete(const std::string& url) override;
  epgstation::HttpResponse Post(const std::string& url, const std::string& jsonBody) override;
  epgstation::HttpResponse Put(const std::string& url, const std::string& jsonBody) override;

private:
  epgstation::HttpResponse Request(const std::string& url,
                                   const char* method,
                                   const std::string* jsonBody = nullptr);
};

// One "Range: bytes=offset-" request at a time over Kodi's curl, for GrowingStream.
class ATTR_DLL_LOCAL KodiRangeFile : public epgstation::IRangeSource
{
public:
  explicit KodiRangeFile(std::string url) : m_url(std::move(url)) {}
  ~KodiRangeFile() override;

  Opened Open(int64_t offset) override;
  int64_t Read(uint8_t* buffer, size_t size) override;
  void Close() override;

private:
  std::string m_url;
  std::unique_ptr<kodi::vfs::CFile> m_file;
};
