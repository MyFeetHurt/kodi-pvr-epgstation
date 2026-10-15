/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Channels.h"

#include <stdexcept>

#include "Json.h"

namespace epgstation
{

std::vector<Channel> ParseChannels(const std::string& json)
{
  const auto doc = nlohmann::json::parse(json, nullptr, false);
  if (!doc.is_array())
    throw std::runtime_error("channels: expected a JSON array");

  std::vector<Channel> result;
  result.reserve(doc.size());
  for (const auto& j : doc)
  {
    Channel c;
    c.id = j.at("id").get<uint64_t>();
    c.networkId = j.at("networkId").get<int>();
    c.serviceId = j.at("serviceId").get<int>();
    c.name = json::StringOr(j, "name");
    c.halfWidthName = json::StringOr(j, "halfWidthName", c.name);
    c.channelType = json::StringOr(j, "channelType");
    c.type = json::NumberOr<int>(j, "type", 0);
    if (j.contains("remoteControlKeyId") && j["remoteControlKeyId"].is_number_integer())
      c.remoteControlKeyId = j["remoteControlKeyId"].get<int>();
    c.hasLogoData = json::BoolOr(j, "hasLogoData", false);
    result.push_back(std::move(c));
  }
  return result;
}

unsigned int KodiUid(int networkId, int serviceId)
{
  return (static_cast<unsigned int>(networkId) & 0xFFFF) << 16 |
         (static_cast<unsigned int>(serviceId) & 0xFFFF);
}

unsigned int KodiUidFromEpgsId(uint64_t epgsId)
{
  return KodiUid(static_cast<int>(epgsId / 100000), static_cast<int>(epgsId % 100000));
}

uint64_t EpgsIdFromUid(unsigned int uid)
{
  return static_cast<uint64_t>(uid >> 16) * 100000 + (uid & 0xFFFF);
}

unsigned int ChannelNumber(const Channel& channel)
{
  if (channel.channelType == "GR" && channel.remoteControlKeyId)
    return static_cast<unsigned int>(*channel.remoteControlKeyId) * 10 +
           (static_cast<unsigned int>(channel.serviceId) & 7) + 1;
  return static_cast<unsigned int>(channel.serviceId);
}

bool IsViewable(const Channel& channel)
{
  return channel.type == 1 || channel.type == 2;
}

std::vector<KodiChannel> ToKodiChannels(const std::vector<Channel>& channels)
{
  std::vector<KodiChannel> result;
  for (const auto& c : channels)
  {
    if (!IsViewable(c))
      continue;
    KodiChannel k;
    k.uid = KodiUid(c.networkId, c.serviceId);
    k.epgsId = c.id;
    k.radio = c.type == 2;
    k.number = ChannelNumber(c);
    k.name = c.halfWidthName.empty() ? c.name : c.halfWidthName;
    k.group = c.channelType;
    k.hasLogo = c.hasLogoData;
    result.push_back(std::move(k));
  }
  return result;
}

std::vector<KodiGroup> ToKodiGroups(const std::vector<KodiChannel>& channels)
{
  std::vector<KodiGroup> groups;
  for (const auto& c : channels)
  {
    KodiGroup* group = nullptr;
    for (auto& g : groups)
      if (g.name == c.group && g.radio == c.radio)
        group = &g;
    if (!group)
    {
      groups.push_back({c.group, c.radio, {}});
      group = &groups.back();
    }
    group->members.push_back(c.uid);
  }
  return groups;
}

} // namespace epgstation
