/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// Pure EPGStation -> Kodi channel mapping. No Kodi headers here: unit-tested on its own.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace epgstation
{

// One /api/channels entry (only the fields we use).
struct Channel
{
  uint64_t id = 0; // EPGStation id = networkId * 100000 + serviceId (does not fit 32 bits)
  int networkId = 0;
  int serviceId = 0;
  std::string name; // full-width
  std::string halfWidthName;
  std::string channelType; // GR, BS, CS, SKY
  int type = 0; // ARIB service_type: 1 digital TV, 2 digital radio, 0xC0 data/one-seg, 0xA4 ...
  std::optional<int> remoteControlKeyId; // GR only
  bool hasLogoData = false;
};

// Throws std::runtime_error on JSON that is not a channel array.
std::vector<Channel> ParseChannels(const std::string& json);

// What Kodi gets for a channel.
struct KodiChannel
{
  unsigned int uid = 0; // see KodiUid()
  uint64_t epgsId = 0;
  bool radio = false;
  unsigned int number = 0; // Japanese 3-digit channel number, see ChannelNumber()
  std::string name;
  std::string group; // channelType
  bool hasLogo = false;
};

// Kodi channel uids are 32-bit: networkId and serviceId are both 16-bit ARIB ids.
unsigned int KodiUid(int networkId, int serviceId);
// EPGStation id (networkId * 100000 + serviceId) <-> Kodi uid.
unsigned int KodiUidFromEpgsId(uint64_t epgsId);
uint64_t EpgsIdFromUid(unsigned int uid);

// GR: remote key * 10 + (serviceId & 7) + 1 (NHK総合1 = 011). BS/CS/SKY: the serviceId.
unsigned int ChannelNumber(const Channel& channel);

// TV and radio services. Data/one-seg services (type 0xC0, 0xA4, ...) are left out.
bool IsViewable(const Channel& channel);

// Viewable channels, in EPGStation's order (GR, BS, CS as the backend sorts them).
std::vector<KodiChannel> ToKodiChannels(const std::vector<Channel>& channels);

struct KodiGroup
{
  std::string name; // channelType
  bool radio = false;
  std::vector<unsigned int> members; // channel uids in channel order
};

// One group per (channelType, radio), in order of first appearance.
std::vector<KodiGroup> ToKodiGroups(const std::vector<KodiChannel>& channels);

} // namespace epgstation
