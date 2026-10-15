/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Live.h"

#include <stdexcept>

#include "Json.h"

namespace epgstation
{

LiveConfig ParseLiveConfig(const std::string& json)
{
  const auto doc = nlohmann::json::parse(json, nullptr, false);
  if (!doc.is_object())
    throw std::runtime_error("config: expected a JSON object");

  LiveConfig config;
  config.enabled = json::BoolOr(doc, "isEnableTSLiveStream", false);
  // streamConfig.live.ts.m2ts: [{name, isUnconverted}]; absent when live streams are off.
  const auto m2ts = doc.value("/streamConfig/live/ts/m2ts"_json_pointer, nlohmann::json::array());
  if (!m2ts.is_array())
    throw std::runtime_error("config: streamConfig.live.ts.m2ts is not an array");
  for (const auto& m : m2ts)
  {
    if (!m.is_object() || !m.contains("name") || !m["name"].is_string())
      throw std::runtime_error("config: m2ts mode without a name");
    config.m2ts.push_back({m["name"].get<std::string>(), json::BoolOr(m, "isUnconverted", false)});
  }
  return config;
}

int SelectLiveMode(const LiveConfig& config, const std::string& wanted)
{
  if (!config.enabled || config.m2ts.empty())
    return -1;
  for (size_t i = 0; i < config.m2ts.size(); ++i)
  {
    if (wanted.empty() ? config.m2ts[i].unconverted : config.m2ts[i].name == wanted)
      return static_cast<int>(i);
  }
  return wanted.empty() ? 0 : -1;
}

std::string LiveModeNames(const LiveConfig& config)
{
  std::string names;
  for (const auto& m : config.m2ts)
    names += (names.empty() ? "" : ", ") + m.name;
  return names;
}

} // namespace epgstation
