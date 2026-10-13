/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// Live TV stream selection from GET /api/config. No Kodi headers: unit-tested on its own.

#include <string>
#include <vector>

namespace epgstation
{

struct LiveMode
{
  std::string name; // "720p", "無変換", ... as configured in EPGStation's config.yml
  bool unconverted = false; // passes the broadcast TS through (no ffmpeg on the server)
};

struct LiveConfig
{
  bool enabled = false; // isEnableTSLiveStream
  std::vector<LiveMode> m2ts; // index = the "mode" query parameter
};

// Throws std::runtime_error when the JSON is not an EPGStation config.
LiveConfig ParseLiveConfig(const std::string& json);

// m2ts mode index for the user's setting: the mode named `wanted`, or for an empty setting the
// first unconverted mode (else mode 0). -1 when live streaming is off or no mode matches.
int SelectLiveMode(const LiveConfig& config, const std::string& wanted);

// "720p, 480p, 無変換" for log messages.
std::string LiveModeNames(const LiveConfig& config);

} // namespace epgstation
