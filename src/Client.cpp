/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Client.h"

#include <kodi/General.h>

#include <algorithm>
#include <ctime>

CEpgstationClient::CEpgstationClient(const kodi::addon::IInstanceInfo& instance)
  : kodi::addon::CInstancePVRClient(instance),
    m_api(m_http, GetInstanceSettingString("url", "http://127.0.0.1:8888")),
    m_liveMode(GetInstanceSettingString("livemode", "")),
    m_preferEncoded(GetInstanceSettingBoolean("preferencoded", true))
{
  m_worker = std::thread(&CEpgstationClient::Worker, this);
}

CEpgstationClient::~CEpgstationClient()
{
  {
    std::lock_guard<std::mutex> lock(m_stopMutex);
    m_stop = true;
  }
  m_stopCv.notify_all();
  if (m_worker.joinable())
    m_worker.join();
}

void CEpgstationClient::CheckConnection()
{
  try
  {
    const std::string version = m_api.Version();
    if (m_state == PVR_CONNECTION_STATE_CONNECTED)
    {
      CheckUpdates();
      return;
    }
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_version = version;
      m_channelsLoaded = false;
    }
    kodi::Log(ADDON_LOG_INFO, "EPGStation %s at %s", version.c_str(), m_api.BaseUrl().c_str());
    m_state = PVR_CONNECTION_STATE_CONNECTED;
    ConnectionStateChange(m_api.BaseUrl(), m_state, "");
    // Kodi may already have asked for channels while the backend was down (and then shows
    // only what its own DB cached): make it ask again.
    TriggerChannelUpdate();
    TriggerChannelGroupsUpdate();
    m_recordedSignature = m_api.Signature();
    m_reservesFingerprint =
        m_api.ReservesFingerprint(std::time(nullptr)) + m_api.RulesFingerprint();
    TriggerRecordingUpdate();
    TriggerTimerUpdate();
    m_epgSnapshot = {}; // the first check after a (re)connect is the baseline
    m_checksSinceEpgCheck = 0;
    CheckEpgChanges();
  }
  catch (const std::exception& e)
  {
    if (m_state == PVR_CONNECTION_STATE_SERVER_UNREACHABLE)
      return;
    kodi::Log(ADDON_LOG_ERROR, "EPGStation not reachable: %s", e.what());
    m_state = PVR_CONNECTION_STATE_SERVER_UNREACHABLE;
    ConnectionStateChange(m_api.BaseUrl(), m_state, "");
  }
}

void CEpgstationClient::CheckUpdates()
{
  bool channels, groups, recordings, timers;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    channels = m_channelsFailed[0] || m_channelsFailed[1];
    groups = m_groupsFailed[0] || m_groupsFailed[1];
    recordings = m_recordingsFailed;
    timers = m_timersFailed;
  }
  if (channels)
  {
    kodi::Log(ADDON_LOG_INFO, "retrying channel update");
    TriggerChannelUpdate();
  }
  if (groups)
  {
    kodi::Log(ADDON_LOG_INFO, "retrying channel group update");
    TriggerChannelGroupsUpdate();
  }

  const epgstation::RecordedSignature signature = m_api.Signature(); // throws when down
  if (signature != m_recordedSignature)
  {
    kodi::Log(ADDON_LOG_DEBUG, "recordings changed: total %d newest %d recording %d encoding %d",
              signature.total, signature.newestId, signature.recording, signature.encoding);
    m_recordedSignature = signature;
    recordings = true;
  }
  if (recordings)
    TriggerRecordingUpdate();

  // Reservations: on a change, and every 10 checks anyway (EPGStation moves reservations with
  // the programme, which the fingerprint does not show).
  const std::string fingerprint =
      m_api.ReservesFingerprint(std::time(nullptr)) + m_api.RulesFingerprint();
  if (fingerprint != m_reservesFingerprint || ++m_checksSinceTimerUpdate >= 10)
  {
    m_reservesFingerprint = fingerprint;
    timers = true;
  }
  if (timers)
  {
    m_checksSinceTimerUpdate = 0;
    TriggerTimerUpdate();
  }

  if (++m_checksSinceEpgCheck >= kEpgCheckEvery)
  {
    m_checksSinceEpgCheck = 0;
    CheckEpgChanges();
  }
}

void CEpgstationClient::CheckEpgChanges()
{
  const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
  epgstation::ScheduleSnapshot snapshot;
  try
  {
    snapshot = m_api.AllSchedules(now, now + kEpgWindowHours * 3600LL * 1000);
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_WARNING, "EPG change check failed: %s", e.what());
    return; // try again next time; Kodi's own EPG refresh still runs
  }

  if (m_epgSnapshot.to > 0)
  {
    std::vector<uint64_t> changed = epgstation::ChangedChannels(m_epgSnapshot, snapshot);
    {
      // Only channels Kodi has (data services etc. are not listed).
      std::lock_guard<std::mutex> lock(m_mutex);
      changed.erase(std::remove_if(changed.begin(), changed.end(),
                                   [this](uint64_t id) {
                                     return std::none_of(m_channels.begin(), m_channels.end(),
                                                         [id](const epgstation::KodiChannel& c) {
                                                           return c.epgsId == id;
                                                         });
                                   }),
                    changed.end());
    }
    if (!changed.empty())
      kodi::Log(ADDON_LOG_INFO, "EPG changed on %zu channel(s): updating them in Kodi",
                changed.size());
    for (const uint64_t id : changed)
      TriggerEpgUpdate(epgstation::KodiUid(static_cast<int>(id / 100000),
                                           static_cast<int>(id % 100000)));
  }
  m_epgSnapshot = std::move(snapshot);
}

void CEpgstationClient::Worker()
{
  // Wake every kRetrySeconds so a failure Kodi hits meanwhile is retried within that time;
  // while all is well, only poll the backend every kCheckSeconds.
  constexpr int ticksPerCheck = kCheckSeconds / kRetrySeconds;
  std::unique_lock<std::mutex> lock(m_stopMutex);
  for (int tick = 0; !m_stop; ++tick)
  {
    lock.unlock();
    bool updateFailed;
    {
      std::lock_guard<std::mutex> stateLock(m_mutex);
      updateFailed = m_channelsFailed[0] || m_channelsFailed[1] || m_groupsFailed[0] ||
                     m_groupsFailed[1] || m_recordingsFailed || m_timersFailed;
    }
    if (m_state != PVR_CONNECTION_STATE_CONNECTED || updateFailed || tick % ticksPerCheck == 0)
      CheckConnection();
    lock.lock();
    m_stopCv.wait_for(lock, std::chrono::seconds(kRetrySeconds), [this] { return m_stop; });
  }
}

ADDON_STATUS CEpgstationClient::SetInstanceSetting(const std::string& settingName,
                                                   const kodi::addon::CSettingValue& settingValue)
{
  // Any change (e.g. a new server URL): let Kodi recreate this instance.
  return ADDON_STATUS_NEED_RESTART;
}

PVR_ERROR CEpgstationClient::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(true);
  capabilities.SetSupportsChannelGroups(true);
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsRecordingsDelete(true);
  capabilities.SetSupportsRecordingsUndelete(false);
  capabilities.SetSupportsRecordingSize(true);
  capabilities.SetHandlesInputStream(true); // recordings in progress (live TV: stream URL)
  capabilities.SetSupportsTimers(true);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetBackendName(std::string& name)
{
  name = "EPGStation";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetBackendVersion(std::string& version)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  version = m_version;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetBackendHostname(std::string& hostname)
{
  hostname = m_api.BaseUrl();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetConnectionString(std::string& connection)
{
  connection = m_api.BaseUrl();
  return PVR_ERROR_NO_ERROR;
}

bool CEpgstationClient::LoadChannelsLocked()
{
  if (m_channelsLoaded)
    return true;
  try
  {
    m_channels = epgstation::ToKodiChannels(m_api.Channels());
    m_groups = epgstation::ToKodiGroups(m_channels);
    m_channelsLoaded = true;
    kodi::Log(ADDON_LOG_INFO, "loaded %zu channels in %zu groups", m_channels.size(),
              m_groups.size());
    return true;
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "loading channels failed: %s", e.what());
    return false;
  }
}

PVR_ERROR CEpgstationClient::GetChannelsAmount(int& amount)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!LoadChannelsLocked())
  {
    m_channelsFailed[0] = m_channelsFailed[1] = true;
    return PVR_ERROR_SERVER_ERROR;
  }
  amount = static_cast<int>(m_channels.size());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  // Kodi asks for channels on every PVR (re)start: refresh from the backend each time.
  m_channelsLoaded = false;
  m_channelsFailed[radio] = !LoadChannelsLocked();
  if (m_channelsFailed[radio])
    return PVR_ERROR_SERVER_ERROR;

  for (const auto& c : m_channels)
  {
    if (c.radio != radio)
      continue;
    kodi::addon::PVRChannel channel;
    channel.SetUniqueId(c.uid);
    channel.SetIsRadio(c.radio);
    channel.SetChannelNumber(c.number);
    channel.SetChannelName(c.name);
    if (c.hasLogo)
      channel.SetIconPath(m_api.ChannelLogoUrl(c.epgsId));
    results.Add(channel);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetChannelGroupsAmount(int& amount)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!LoadChannelsLocked())
  {
    m_groupsFailed[0] = m_groupsFailed[1] = true;
    return PVR_ERROR_SERVER_ERROR;
  }
  amount = static_cast<int>(m_groups.size());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetChannelGroups(bool radio,
                                              kodi::addon::PVRChannelGroupsResultSet& results)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_groupsFailed[radio] = !LoadChannelsLocked();
  if (m_groupsFailed[radio])
    return PVR_ERROR_SERVER_ERROR;

  unsigned int position = 0;
  for (const auto& g : m_groups)
  {
    if (g.radio != radio)
      continue;
    kodi::addon::PVRChannelGroup group;
    group.SetGroupName(g.name);
    group.SetIsRadio(g.radio);
    group.SetPosition(++position);
    results.Add(group);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetChannelGroupMembers(
    const kodi::addon::PVRChannelGroup& group,
    kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!LoadChannelsLocked())
  {
    m_groupsFailed[group.GetIsRadio()] = true;
    return PVR_ERROR_SERVER_ERROR;
  }

  for (const auto& g : m_groups)
  {
    if (g.name != group.GetGroupName() || g.radio != group.GetIsRadio())
      continue;
    for (const unsigned int uid : g.members)
    {
      for (const auto& c : m_channels)
      {
        if (c.uid != uid)
          continue;
        kodi::addon::PVRChannelGroupMember member;
        member.SetGroupName(g.name);
        member.SetChannelUniqueId(c.uid);
        member.SetChannelNumber(c.number);
        results.Add(member);
      }
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetEPGForChannel(int channelUid,
                                              time_t start,
                                              time_t end,
                                              kodi::addon::PVREPGTagsResultSet& results)
{
  // No m_mutex: m_api is stateless, and holding the lock for ~120 sequential requests would
  // block every other Kodi call during an EPG refresh.
  const unsigned int uid = static_cast<unsigned int>(channelUid);
  std::vector<epgstation::KodiEpgTag> tags;
  try
  {
    tags = epgstation::ToKodiEpg(
        m_api.ChannelSchedule(epgstation::EpgsIdFromUid(uid), static_cast<int64_t>(start) * 1000,
                              epgstation::ScheduleDays(start, end)),
        start, end);
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "EPG for channel %u failed: %s", uid, e.what());
    return PVR_ERROR_SERVER_ERROR;
  }

  for (const auto& t : tags)
  {
    kodi::addon::PVREPGTag tag;
    tag.SetUniqueBroadcastId(t.broadcastId);
    tag.SetUniqueChannelId(uid);
    tag.SetTitle(t.title);
    tag.SetStartTime(t.start);
    tag.SetEndTime(t.end);
    tag.SetPlotOutline(t.plotOutline);
    tag.SetPlot(t.plot);
    // Type picks Kodi's guide colour; EPG_GENRE_USE_STRING as sub type makes Kodi show our
    // Japanese ARIB genre names instead of its DVB names.
    tag.SetGenreType(t.genreType);
    tag.SetGenreSubType(EPG_GENRE_USE_STRING);
    tag.SetGenreDescription(t.genreDescription);
    tag.SetFlags(t.flags);
    results.Add(tag);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetChannelStreamProperties(
    const kodi::addon::PVRChannel& channel,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  // Kodi plays the URL itself: EPGStation tunes when Kodi connects and frees the tuner when
  // Kodi closes the connection (channel change or stop). No m_mutex: m_api is stateless.
  const uint64_t epgsId = epgstation::EpgsIdFromUid(channel.GetUniqueId());
  int mode;
  try
  {
    const epgstation::LiveConfig config = m_api.Config();
    mode = epgstation::SelectLiveMode(config, m_liveMode);
    if (mode < 0)
    {
      // A settings problem the user can fix: say so on screen, not only in the log.
      if (!config.enabled)
      {
        kodi::Log(ADDON_LOG_ERROR, "live TV is disabled in EPGStation (isEnableTSLiveStream)");
        kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30013));
      }
      else
      {
        const std::string names = epgstation::LiveModeNames(config);
        kodi::Log(ADDON_LOG_ERROR, "live stream mode \"%s\" not found; EPGStation has: %s",
                  m_liveMode.c_str(), names.c_str());
        kodi::QueueFormattedNotification(QUEUE_ERROR,
                                         kodi::addon::GetLocalizedString(30014).c_str(),
                                         m_liveMode.c_str(), names.c_str());
      }
      return PVR_ERROR_FAILED;
    }
    kodi::Log(ADDON_LOG_INFO, "live: %s (%llu) mode %d %s", channel.GetChannelName().c_str(),
              static_cast<unsigned long long>(epgsId), mode, config.m2ts[mode].name.c_str());
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "live stream for channel %u failed: %s", channel.GetUniqueId(),
              e.what());
    return PVR_ERROR_SERVER_ERROR;
  }

  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, m_api.LiveStreamUrl(epgsId, mode));
  properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "video/mp2t");
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, "true");
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetRecordingsAmount(bool deleted, int& amount)
{
  if (deleted)
  {
    amount = 0; // EPGStation has no trash
    return PVR_ERROR_NO_ERROR;
  }
  try
  {
    const epgstation::RecordedSignature signature = m_api.Signature();
    amount = signature.total + signature.recording;
    return PVR_ERROR_NO_ERROR;
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "recordings amount failed: %s", e.what());
    return PVR_ERROR_SERVER_ERROR;
  }
}

PVR_ERROR CEpgstationClient::GetRecordings(bool deleted,
                                           kodi::addon::PVRRecordingsResultSet& results)
{
  if (deleted)
    return PVR_ERROR_NO_ERROR;

  // The list without m_mutex (a few requests); channel names with it.
  std::vector<epgstation::Recording> recordings;
  try
  {
    recordings = m_api.Recorded();
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "loading recordings failed: %s", e.what());
    std::lock_guard<std::mutex> lock(m_mutex);
    m_recordingsFailed = true;
    return PVR_ERROR_SERVER_ERROR;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  m_recordingsFailed = false;
  LoadChannelsLocked(); // names only: recordings are listed even without them
  for (const auto& r : recordings)
  {
    const epgstation::KodiRecording k = epgstation::ToKodiRecording(r, m_preferEncoded);
    kodi::addon::PVRRecording recording;
    recording.SetRecordingId(k.recordingId);
    recording.SetTitle(k.title);
    recording.SetPlotOutline(k.plotOutline);
    recording.SetPlot(k.plot);
    recording.SetGenreType(k.genreType);
    recording.SetGenreSubType(EPG_GENRE_USE_STRING);
    recording.SetGenreDescription(k.genreDescription);
    recording.SetRecordingTime(k.recordingTime);
    recording.SetDuration(k.duration);
    recording.SetSizeInBytes(k.size);
    if (k.epgEventId)
      recording.SetEPGEventId(k.epgEventId);
    if (k.thumbnailId >= 0)
      recording.SetThumbnailPath(m_api.ThumbnailUrl(k.thumbnailId));

    recording.SetChannelType(PVR_RECORDING_CHANNEL_TYPE_UNKNOWN);
    for (const auto& c : m_channels)
    {
      if (k.channelUid && c.uid == k.channelUid)
      {
        recording.SetChannelUid(static_cast<int>(c.uid));
        recording.SetChannelName(c.name);
        recording.SetChannelType(c.radio ? PVR_RECORDING_CHANNEL_TYPE_RADIO
                                         : PVR_RECORDING_CHANNEL_TYPE_TV);
        if (c.hasLogo)
          recording.SetIconPath(m_api.ChannelLogoUrl(c.epgsId));
        break;
      }
    }
    if (recording.GetChannelType() == PVR_RECORDING_CHANNEL_TYPE_UNKNOWN)
      recording.SetChannelUid(PVR_CHANNEL_INVALID_UID);
    results.Add(recording);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::DeleteRecording(const kodi::addon::PVRRecording& recording)
{
  const int id = std::atoi(recording.GetRecordingId().c_str());
  try
  {
    // EPGStation refuses protected recordings and stops one still being recorded first.
    m_api.DeleteRecorded(id);
    kodi::Log(ADDON_LOG_INFO, "deleted recording %d %s", id, recording.GetTitle().c_str());
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "deleting recording %d failed: %s", id, e.what());
    if (std::string(e.what()).find("RecordedIsProtected") != std::string::npos)
      kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30023));
    return PVR_ERROR_FAILED;
  }
  TriggerRecordingUpdate();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetRecordingSize(const kodi::addon::PVRRecording& recording,
                                              int64_t& size)
{
  try
  {
    const epgstation::Recording r =
        m_api.RecordedItem(std::atoi(recording.GetRecordingId().c_str()));
    const epgstation::VideoFile* file = epgstation::PickVideoFile(r, m_preferEncoded);
    size = file ? file->size : 0;
    return PVR_ERROR_NO_ERROR;
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "recording size failed: %s", e.what());
    return PVR_ERROR_SERVER_ERROR;
  }
}

PVR_ERROR CEpgstationClient::GetRecordingStreamProperties(
    const kodi::addon::PVRRecording& recording,
    std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  // Fresh from EPGStation: an encode may have finished (or the TS been deleted) since Kodi
  // loaded its list.
  const int id = std::atoi(recording.GetRecordingId().c_str());
  epgstation::Recording r;
  try
  {
    r = m_api.RecordedItem(id);
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "recording %d failed: %s", id, e.what());
    return PVR_ERROR_SERVER_ERROR;
  }
  const epgstation::VideoFile* file = epgstation::PickVideoFile(r, m_preferEncoded);
  if (!file)
  {
    kodi::Log(ADDON_LOG_ERROR, "recording %d has no video file", id);
    return PVR_ERROR_FAILED;
  }
  if (r.isRecording)
  {
    // No URL: Kodi calls OpenRecordedStream, which follows the growing file.
    kodi::Log(ADDON_LOG_INFO, "recording %d: still recording, read by the addon", id);
    return PVR_ERROR_NO_ERROR;
  }
  kodi::Log(ADDON_LOG_INFO, "recording %d: file %d %s (%s)", id, file->id, file->name.c_str(),
            file->type.c_str());

  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, m_api.VideoUrl(file->id));
  if (file->type == "ts")
    properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "video/mp2t");
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, "false");
  return PVR_ERROR_NO_ERROR;
}

bool CEpgstationClient::OpenRecordedStream(const kodi::addon::PVRRecording& recording)
{
  const int id = std::atoi(recording.GetRecordingId().c_str());
  m_streamStart = 0;
  m_streamCancel = true; // a previous stream's Read may still wait
  std::lock_guard<std::mutex> lock(m_streamMutex);
  m_streamCancel = false;
  m_stream.reset();
  m_streamSource.reset();
  try
  {
    const epgstation::Recording r = m_api.RecordedItem(id);
    // The TS: an encoded file only exists once the recording is finished.
    const epgstation::VideoFile* file = epgstation::PickVideoFile(r, false);
    if (!file)
    {
      kodi::Log(ADDON_LOG_ERROR, "recording %d has no video file", id);
      return false;
    }
    m_streamSource = std::make_unique<KodiRangeFile>(m_api.VideoUrl(file->id));
    m_stream = std::make_unique<epgstation::GrowingStream>(
        *m_streamSource,
        [this, id] {
          try
          {
            return m_api.RecordedItem(id).isRecording;
          }
          catch (const std::exception& e)
          {
            kodi::Log(ADDON_LOG_WARNING, "recording %d state: %s", id, e.what());
            return true; // keep waiting; GrowingStream gives up after kMaxWaitMs
          }
        },
        [this](int ms) {
          // In short slices so CloseRecordedStream is not kept waiting.
          for (int slept = 0; slept < ms && !m_streamCancel; slept += 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        },
        [this] { return m_streamCancel.load(); });
    if (!m_stream->Open())
    {
      kodi::Log(ADDON_LOG_ERROR, "recording %d: opening file %d failed", id, file->id);
      m_stream.reset();
      m_streamSource.reset();
      return false;
    }
    m_streamEnd = static_cast<time_t>(r.program.endAt / 1000);
    m_streamStart = static_cast<time_t>(r.program.startAt / 1000); // last: marks "open"
    kodi::Log(ADDON_LOG_INFO, "recording %d: following file %d while recording (%lld bytes)", id,
              file->id, static_cast<long long>(m_stream->Length()));
    return true;
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "recording %d failed: %s", id, e.what());
    return false;
  }
}

void CEpgstationClient::CloseRecordedStream()
{
  m_streamStart = 0;
  m_streamCancel = true;
  std::lock_guard<std::mutex> lock(m_streamMutex);
  m_streamCancel = false;
  m_stream.reset();
  m_streamSource.reset();
}

int CEpgstationClient::ReadRecordedStream(unsigned char* buffer, unsigned int size)
{
  std::lock_guard<std::mutex> lock(m_streamMutex);
  return m_stream ? m_stream->Read(buffer, size) : -1;
}

int64_t CEpgstationClient::SeekRecordedStream(int64_t position, int whence)
{
  std::lock_guard<std::mutex> lock(m_streamMutex);
  return m_stream ? m_stream->Seek(position, whence) : -1;
}

int64_t CEpgstationClient::LengthRecordedStream()
{
  std::lock_guard<std::mutex> lock(m_streamMutex);
  return m_stream ? m_stream->Length() : -1;
}

PVR_ERROR CEpgstationClient::GetStreamTimes(kodi::addon::PVRStreamTimes& times)
{
  const time_t start = m_streamStart;
  if (!start)
    return PVR_ERROR_NOT_IMPLEMENTED;
  const time_t now = std::min(std::time(nullptr), static_cast<time_t>(m_streamEnd));
  times.SetStartTime(start);
  times.SetPTSStart(0);
  times.SetPTSBegin(0);
  times.SetPTSEnd(static_cast<int64_t>(std::max<time_t>(0, now - start)) *
                  STREAM_TIME_BASE);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  constexpr uint64_t times = PVR_TIMER_TYPE_SUPPORTS_CHANNELS | PVR_TIMER_TYPE_SUPPORTS_START_TIME |
                             PVR_TIMER_TYPE_SUPPORTS_END_TIME;

  // Order matters: Kodi's "Add timer..." starts with the first type, and a guide type there
  // has no programme to reserve.
  kodi::addon::PVRTimerType manual;
  manual.SetId(epgstation::timer_type::kManualOnce);
  manual.SetAttributes(PVR_TIMER_TYPE_IS_MANUAL | PVR_TIMER_TYPE_FORBIDS_EPG_TAG_ON_CREATE | times);
  manual.SetDescription(kodi::addon::GetLocalizedString(30031));
  types.push_back(manual);

  kodi::addon::PVRTimerType epg;
  epg.SetId(epgstation::timer_type::kEpgOnce);
  epg.SetAttributes(PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE | times);
  epg.SetDescription(kodi::addon::GetLocalizedString(30030));
  types.push_back(epg);

  // Made by an EPGStation rule: not edited here; disabling skips it in EPGStation (deleting
  // does the same).
  kodi::addon::PVRTimerType ruleOnce;
  ruleOnce.SetId(epgstation::timer_type::kRuleOnce);
  ruleOnce.SetAttributes(PVR_TIMER_TYPE_IS_READONLY | PVR_TIMER_TYPE_FORBIDS_NEW_INSTANCES |
                         PVR_TIMER_TYPE_SUPPORTS_READONLY_DELETE |
                         PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE | times);
  ruleOnce.SetDescription(kodi::addon::GetLocalizedString(30032));
  types.push_back(ruleOnce);

  // Rules (EPGStation keyword rules).
  const std::vector<kodi::addon::PVRTypeIntValue> duplicates = {
      {0, kodi::addon::GetLocalizedString(30037)}, {1, kodi::addon::GetLocalizedString(30038)}};
  constexpr uint64_t rule = PVR_TIMER_TYPE_IS_REPEATING | PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE |
                            PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH |
                            PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH |
                            PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS |
                            PVR_TIMER_TYPE_SUPPORTS_START_ANYTIME |
                            PVR_TIMER_TYPE_SUPPORTS_END_ANYTIME |
                            PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES | times;

  kodi::addon::PVRTimerType keyword;
  keyword.SetId(epgstation::timer_type::kRuleKeyword);
  keyword.SetAttributes(rule | PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL |
                        PVR_TIMER_TYPE_FORBIDS_EPG_TAG_ON_CREATE);
  keyword.SetDescription(kodi::addon::GetLocalizedString(30035));
  keyword.SetPreventDuplicateEpisodes(duplicates, 0);
  types.push_back(keyword);

  kodi::addon::PVRTimerType series;
  series.SetId(epgstation::timer_type::kRuleSeries);
  series.SetAttributes(rule | PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE);
  series.SetDescription(kodi::addon::GetLocalizedString(30036));
  series.SetPreventDuplicateEpisodes(duplicates, 0);
  types.push_back(series);

  kodi::addon::PVRTimerType readOnly;
  readOnly.SetId(epgstation::timer_type::kRuleReadOnly);
  readOnly.SetAttributes(PVR_TIMER_TYPE_IS_REPEATING | PVR_TIMER_TYPE_IS_READONLY |
                         PVR_TIMER_TYPE_FORBIDS_NEW_INSTANCES |
                         PVR_TIMER_TYPE_SUPPORTS_READONLY_DELETE |
                         PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE |
                         PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH);
  readOnly.SetDescription(kodi::addon::GetLocalizedString(30039));
  types.push_back(readOnly);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::GetTimersAmount(int& amount)
{
  try
  {
    amount = static_cast<int>(m_api.Reserves().size() + m_api.Rules().size());
    return PVR_ERROR_NO_ERROR;
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "timers amount failed: %s", e.what());
    return PVR_ERROR_SERVER_ERROR;
  }
}

PVR_ERROR CEpgstationClient::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  std::vector<epgstation::Reserve> reserves;
  std::vector<epgstation::Rule> rules;
  try
  {
    reserves = m_api.Reserves();
    rules = m_api.Rules();
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "loading reservations failed: %s", e.what());
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timersFailed = true;
    return PVR_ERROR_SERVER_ERROR;
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timersFailed = false;
  }

  const time_t now = std::time(nullptr);
  for (const auto& r : reserves)
  {
    const epgstation::KodiTimer t = epgstation::ToKodiTimer(r, now);
    kodi::addon::PVRTimer timer;
    timer.SetClientIndex(t.clientIndex);
    timer.SetTimerType(t.type);
    timer.SetState(static_cast<PVR_TIMER_STATE>(t.state));
    timer.SetClientChannelUid(static_cast<int>(t.channelUid));
    timer.SetTitle(t.title);
    timer.SetSummary(t.summary);
    timer.SetStartTime(t.start);
    timer.SetEndTime(t.end);
    if (t.epgUid)
      timer.SetEPGUid(t.epgUid);
    if (t.ruleId >= 0)
      timer.SetParentClientIndex(epgstation::kRuleIndexBase + static_cast<unsigned int>(t.ruleId));
    results.Add(timer);
  }
  for (const auto& r : rules)
  {
    kodi::addon::PVRTimer timer;
    FillRuleTimer(r, timer);
    results.Add(timer);
  }
  return PVR_ERROR_NO_ERROR;
}

void CEpgstationClient::FillRuleTimer(const epgstation::Rule& rule, kodi::addon::PVRTimer& timer)
{
  timer.SetClientIndex(epgstation::kRuleIndexBase + static_cast<unsigned int>(rule.id));
  timer.SetTimerType(rule.editable ? epgstation::timer_type::kRuleKeyword
                                   : epgstation::timer_type::kRuleReadOnly);
  timer.SetState(rule.enabled ? PVR_TIMER_STATE_SCHEDULED : PVR_TIMER_STATE_DISABLED);
  timer.SetTitle(rule.keyword.empty() ? "#" + std::to_string(rule.id) : rule.keyword);
  timer.SetEPGSearchString(rule.keyword);
  timer.SetFullTextEpgSearch(rule.fullText);
  timer.SetClientChannelUid(
      rule.channelId ? static_cast<int>(epgstation::KodiUid(
                           static_cast<int>(rule.channelId / 100000),
                           static_cast<int>(rule.channelId % 100000)))
                     : PVR_TIMER_ANY_CHANNEL);
  timer.SetWeekdays(rule.kodiWeekdays);
  timer.SetPreventDuplicateEpisodes(rule.avoidDuplicate ? 1 : 0);
  timer.SetStartAnyTime(rule.anyTime);
  timer.SetEndAnyTime(rule.anyTime);
  // Kodi only uses the time of day of a rule's start and end: today at those (local) hours.
  const time_t now = std::time(nullptr);
  std::tm day = *std::localtime(&now);
  day.tm_min = day.tm_sec = 0;
  day.tm_hour = rule.anyTime ? 0 : rule.startHour;
  timer.SetStartTime(std::mktime(&day));
  day.tm_hour = rule.anyTime ? 23 : rule.endHour;
  day.tm_min = rule.anyTime ? 59 : 0;
  timer.SetEndTime(std::mktime(&day));
}

epgstation::RuleEdit CEpgstationClient::RuleEditFromTimer(const kodi::addon::PVRTimer& timer)
{
  epgstation::RuleEdit e;
  // The search text only: Kodi names a new rule after its channel when the name is left as is.
  e.keyword = timer.GetEPGSearchString();
  // "Record series": Kodi fills the search text with the programme title; a series keyword
  // finds the other episodes. Text the user typed is kept as it is.
  if (timer.GetTimerType() == epgstation::timer_type::kRuleSeries &&
      (e.keyword.empty() || e.keyword == timer.GetTitle()))
    e.keyword = epgstation::SeriesKeyword(timer.GetTitle());
  e.fullText = timer.GetFullTextEpgSearch();
  const int uid = timer.GetClientChannelUid();
  e.channelId = uid == PVR_TIMER_ANY_CHANNEL || uid <= 0
                    ? 0
                    : epgstation::EpgsIdFromUid(static_cast<unsigned int>(uid));
  e.kodiWeekdays = timer.GetWeekdays();
  e.anyTime = timer.GetStartAnyTime() || timer.GetEndAnyTime();
  const time_t start = timer.GetStartTime();
  const time_t end = timer.GetEndTime();
  e.startHour = std::localtime(&start)->tm_hour;
  e.endHour = std::localtime(&end)->tm_hour;
  if (e.startHour == e.endHour)
    e.anyTime = true; // a window of whole hours: less than one hour means any time
  e.enabled = timer.GetState() != PVR_TIMER_STATE_DISABLED;
  e.avoidDuplicate = timer.GetPreventDuplicateEpisodes() != 0;
  return e;
}

PVR_ERROR CEpgstationClient::AddTimer(const kodi::addon::PVRTimer& timer)
{
  const unsigned int uid = static_cast<unsigned int>(timer.GetClientChannelUid());
  std::string body;
  switch (timer.GetTimerType())
  {
    case epgstation::timer_type::kEpgOnce:
      if (timer.GetEPGUid() == 0 || timer.GetEPGUid() == PVR_TIMER_NO_EPG_UID)
        return PVR_ERROR_INVALID_PARAMETERS;
      body = epgstation::EpgReserveBody(epgstation::ProgramId(uid, timer.GetEPGUid()));
      break;
    case epgstation::timer_type::kManualOnce:
    {
      // Kodi sends start time 0 for an instant timer ("Record" on a guide gap or a channel).
      const time_t start = timer.GetStartTime() == 0 ? std::time(nullptr) : timer.GetStartTime();
      if (timer.GetEndTime() <= start)
        return PVR_ERROR_INVALID_PARAMETERS;
      body = epgstation::ManualReserveBody(
          timer.GetTitle().empty() ? kodi::addon::GetLocalizedString(30031) : timer.GetTitle(),
          epgstation::EpgsIdFromUid(uid), start, timer.GetEndTime());
      break;
    }
    case epgstation::timer_type::kRuleKeyword:
    case epgstation::timer_type::kRuleSeries:
    {
      const epgstation::RuleEdit e = RuleEditFromTimer(timer);
      if (e.keyword.empty())
      {
        kodi::QueueNotification(QUEUE_WARNING, "", kodi::addon::GetLocalizedString(30040));
        return PVR_ERROR_INVALID_PARAMETERS;
      }
      try
      {
        const int id = m_api.AddRule(epgstation::NewRuleBody(e));
        kodi::Log(ADDON_LOG_INFO, "added rule %d: %s", id, e.keyword.c_str());
      }
      catch (const std::exception& ex)
      {
        kodi::Log(ADDON_LOG_ERROR, "adding rule %s failed: %s", e.keyword.c_str(), ex.what());
        kodi::QueueFormattedNotification(QUEUE_ERROR,
                                         kodi::addon::GetLocalizedString(30034).c_str(),
                                         e.keyword.c_str());
        return PVR_ERROR_SERVER_ERROR;
      }
      TriggerTimerUpdate();
      return PVR_ERROR_NO_ERROR;
    }
    default:
      return PVR_ERROR_NOT_IMPLEMENTED;
  }

  try
  {
    const int id = m_api.AddReserve(body);
    kodi::Log(ADDON_LOG_INFO, "reserved %d: %s", id, timer.GetTitle().c_str());
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "reserving %s failed: %s", timer.GetTitle().c_str(), e.what());
    kodi::QueueFormattedNotification(QUEUE_ERROR, kodi::addon::GetLocalizedString(30034).c_str(),
                                     timer.GetTitle().c_str());
    return PVR_ERROR_SERVER_ERROR;
  }
  TriggerTimerUpdate();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete)
{
  if (timer.GetClientIndex() >= epgstation::kRuleIndexBase)
  {
    // EPGStation removes the rule's future reservations with it.
    const int ruleId = static_cast<int>(timer.GetClientIndex() - epgstation::kRuleIndexBase);
    try
    {
      m_api.DeleteRule(ruleId);
      kodi::Log(ADDON_LOG_INFO, "deleted rule %d %s", ruleId, timer.GetTitle().c_str());
    }
    catch (const std::exception& e)
    {
      kodi::Log(ADDON_LOG_ERROR, "deleting rule %d failed: %s", ruleId, e.what());
      return PVR_ERROR_SERVER_ERROR;
    }
    TriggerTimerUpdate();
    return PVR_ERROR_NO_ERROR;
  }

  // Deleting a reservation that is recording stops the recording: Kodi wants to ask the user
  // first, so say so unless it already did (forceDelete).
  const time_t now = std::time(nullptr);
  if (!forceDelete && (timer.GetState() == PVR_TIMER_STATE_RECORDING ||
                       (now >= timer.GetStartTime() && now < timer.GetEndTime() &&
                        timer.GetState() != PVR_TIMER_STATE_DISABLED)))
    return PVR_ERROR_RECORDING_RUNNING;
  try
  {
    m_api.DeleteReserve(static_cast<int>(timer.GetClientIndex()));
    kodi::Log(ADDON_LOG_INFO, "deleted reservation %u %s", timer.GetClientIndex(),
              timer.GetTitle().c_str());
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "deleting reservation %u failed: %s", timer.GetClientIndex(),
              e.what());
    return PVR_ERROR_SERVER_ERROR;
  }
  TriggerTimerUpdate();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR CEpgstationClient::UpdateTimer(const kodi::addon::PVRTimer& timer)
{
  const bool enable = timer.GetState() != PVR_TIMER_STATE_DISABLED;
  try
  {
    switch (timer.GetTimerType())
    {
      case epgstation::timer_type::kRuleOnce:
        // A rule's reservation: disabled = skipped in EPGStation.
        if (enable)
          m_api.UnskipReserve(static_cast<int>(timer.GetClientIndex()));
        else
          m_api.DeleteReserve(static_cast<int>(timer.GetClientIndex()));
        break;
      case epgstation::timer_type::kRuleKeyword:
      case epgstation::timer_type::kRuleSeries:
      {
        // Read the rule fresh and change only what Kodi shows (genres, folders, encoding...
        // set in EPGStation are kept).
        const int ruleId = static_cast<int>(timer.GetClientIndex() - epgstation::kRuleIndexBase);
        const epgstation::RuleEdit edit = RuleEditFromTimer(timer);
        if (edit.keyword.empty()) // would match every programme
        {
          kodi::QueueNotification(QUEUE_WARNING, "", kodi::addon::GetLocalizedString(30040));
          return PVR_ERROR_INVALID_PARAMETERS;
        }
        const epgstation::Rule current = m_api.GetRule(ruleId);
        m_api.UpdateRule(ruleId, epgstation::UpdatedRuleBody(current.raw, edit));
        break;
      }
      case epgstation::timer_type::kRuleReadOnly:
        m_api.SetRuleEnabled(
            static_cast<int>(timer.GetClientIndex() - epgstation::kRuleIndexBase), enable);
        break;
      case epgstation::timer_type::kManualOnce:
      {
        // EPGStation cannot change a reservation: replace it (new one first, so a failure
        // leaves the old one).
        const PVR_ERROR added = AddTimer(timer);
        if (added != PVR_ERROR_NO_ERROR)
          return added;
        return DeleteTimer(timer, true);
      }
      default:
        kodi::QueueNotification(QUEUE_WARNING, "", kodi::addon::GetLocalizedString(30033));
        return PVR_ERROR_NOT_IMPLEMENTED;
    }
  }
  catch (const std::exception& e)
  {
    kodi::Log(ADDON_LOG_ERROR, "updating timer %u failed: %s", timer.GetClientIndex(), e.what());
    return PVR_ERROR_SERVER_ERROR;
  }
  kodi::Log(ADDON_LOG_INFO, "updated timer %u %s", timer.GetClientIndex(), timer.GetTitle().c_str());
  TriggerTimerUpdate();
  return PVR_ERROR_NO_ERROR;
}
