/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "KodiHttp.h"
#include "epgstation/Api.h"
#include "epgstation/Channels.h"

#include <atomic>
#include <condition_variable>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <kodi/addon-instance/PVR.h>

// One PVR client per Kodi add-on instance (= one EPGStation server).
// Kodi calls these from several threads: all shared state is guarded by m_mutex.
class ATTR_DLL_LOCAL CEpgstationClient : public kodi::addon::CInstancePVRClient
{
public:
  explicit CEpgstationClient(const kodi::addon::IInstanceInfo& instance);
  ~CEpgstationClient() override;

  ADDON_STATUS SetInstanceSetting(const std::string& settingName,
                                  const kodi::addon::CSettingValue& settingValue) override;

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetBackendHostname(std::string& hostname) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;
  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used) override; // KiB

  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) override;
  PVR_ERROR GetChannelGroupsAmount(int& amount) override;
  PVR_ERROR GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                   kodi::addon::PVRChannelGroupMembersResultSet& results) override;

  PVR_ERROR GetEPGForChannel(int channelUid,
                             time_t start,
                             time_t end,
                             kodi::addon::PVREPGTagsResultSet& results) override;

  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR GetRecordingSize(const kodi::addon::PVRRecording& recording, int64_t& size) override;
  PVR_ERROR GetRecordingStreamProperties(
      const kodi::addon::PVRRecording& recording,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override;

  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;
  PVR_ERROR GetTimersAmount(int& amount) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) override;
  PVR_ERROR UpdateTimer(const kodi::addon::PVRTimer& timer) override;

  // EPGStation rule <-> Kodi timer rule fields.
  static epgstation::RuleEdit RuleEditFromTimer(const kodi::addon::PVRTimer& timer);
  static void FillRuleTimer(const epgstation::Rule& rule, kodi::addon::PVRTimer& timer);

  // Recordings still being written are read by the addon (GrowingStream); finished ones are
  // played by Kodi from their URL. Kodi 22 (PVR API 9) passes a stream id; without
  // SupportsMultipleRecordedStreams it closes one stream before opening the next, so there is
  // still only one (kRecordedStreamId).
#if PVR_API_MAJOR >= 9
  bool OpenRecordedStream(const kodi::addon::PVRRecording& recording, int64_t& streamId) override
  {
    streamId = kRecordedStreamId;
    return OpenRecorded(recording);
  }
  void CloseRecordedStream(int64_t) override { CloseRecorded(); }
  int ReadRecordedStream(int64_t, unsigned char* buffer, unsigned int size) override
  {
    return ReadRecorded(buffer, size);
  }
  int64_t SeekRecordedStream(int64_t, int64_t position, int whence) override
  {
    return SeekRecorded(position, whence);
  }
  int64_t LengthRecordedStream(int64_t) override { return LengthRecorded(); }
#else
  bool OpenRecordedStream(const kodi::addon::PVRRecording& recording) override
  {
    return OpenRecorded(recording);
  }
  void CloseRecordedStream() override { CloseRecorded(); }
  int ReadRecordedStream(unsigned char* buffer, unsigned int size) override
  {
    return ReadRecorded(buffer, size);
  }
  int64_t SeekRecordedStream(int64_t position, int whence) override
  {
    return SeekRecorded(position, whence);
  }
  int64_t LengthRecordedStream() override { return LengthRecorded(); }
#endif
  // Live TV read by the add-on (Kodi 22; Kodi 21 plays the URL from GetChannelStreamProperties).
  bool OpenLiveStream(const kodi::addon::PVRChannel& channel) override;
  void CloseLiveStream() override;
  int ReadLiveStream(unsigned char* buffer, unsigned int size) override;
  int64_t SeekLiveStream(int64_t, int) override { return -1; }
  int64_t LengthLiveStream() override { return -1; }

  // Duration recorded so far, for Kodi's progress bar and time seeks while recording (Kodi 22
  // falls back to this from GetRecordedStreamTimes for single-stream clients).
  PVR_ERROR GetStreamTimes(kodi::addon::PVRStreamTimes& times) override;
  // Live TV: no pause (EPGStation's stream would back up) or seek; recordings: both.
  bool CanPauseStream() override { return !m_liveOpen; }
  bool CanSeekStream() override { return !m_liveOpen; }
  bool IsRealTimeStream() override { return m_liveOpen; }

#if PVR_API_MAJOR >= 9
  // source: also PVR_SOURCE_EPG_AS_LIVE (a guide entry played as live); the stream is the same.
  PVR_ERROR GetChannelStreamProperties(
      const kodi::addon::PVRChannel& channel,
      PVR_SOURCE,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override
  {
    return ChannelStreamProperties(channel, properties);
  }
#else
  PVR_ERROR GetChannelStreamProperties(
      const kodi::addon::PVRChannel& channel,
      std::vector<kodi::addon::PVRStreamProperty>& properties) override
  {
    return ChannelStreamProperties(channel, properties);
  }
#endif

private:
  static constexpr int64_t kRecordedStreamId = 1;
  bool OpenRecorded(const kodi::addon::PVRRecording& recording);
  void CloseRecorded();
  int ReadRecorded(unsigned char* buffer, unsigned int size);
  int64_t SeekRecorded(int64_t position, int whence);
  int64_t LengthRecorded();
  PVR_ERROR ChannelStreamProperties(const kodi::addon::PVRChannel& channel,
                                    std::vector<kodi::addon::PVRStreamProperty>& properties);
  // m2ts URL of the channel in the configured mode; "" (logged, notified) when there is none.
  std::string LiveUrl(const kodi::addon::PVRChannel& channel);

  // Fetches /api/channels when not loaded yet. Caller holds m_mutex.
  bool LoadChannelsLocked();

  // GET /api/version; reports connection state changes to Kodi. Kodi only loads channels from
  // a connected client, so a backend that was down at startup must be noticed coming back.
  void CheckConnection();
  // While connected: re-trigger failed updates, and a recordings update when the recordings
  // fingerprint changed (new, finished, deleted, encoded).
  void CheckUpdates();
  // Wakes every kRetrySeconds: CheckConnection then if down or an update failed,
  // otherwise every kCheckSeconds.
  void Worker();

  static constexpr int kRetrySeconds = 10;
  static constexpr int kCheckSeconds = 60;

  std::mutex m_mutex;
  KodiHttp m_http;
  epgstation::Api m_api;
  const std::string m_liveMode; // EPGStation m2ts mode name; "" = unconverted
  const bool m_preferEncoded; // play a recording's encoded file when there is one
  std::string m_version;
  bool m_channelsLoaded = false;
  time_t m_channelsLoadedAt = 0;
  static constexpr int kChannelsFreshSeconds = 10;
  // Kodi's last GetChannels / GetChannelGroups(+Members) call failed, per [radio]: the worker
  // re-triggers that update until the same call succeeds. Only Kodi's own calls clear these;
  // a successful load for another call does not mean Kodi has the channels.
  bool m_channelsFailed[2] = {false, false};
  bool m_groupsFailed[2] = {false, false};
  bool m_recordingsFailed = false; // same for GetRecordings
  bool m_timersFailed = false; // same for GetTimers
  // Kodi added, changed or deleted a reservation or rule: the worker refreshes Kodi's timers once
  // more on its next wake (EPGStation makes or drops a rule's reservations a moment after
  // answering) and takes that state as its fingerprint baseline (else a reservation added in Kodi
  // and deleted elsewhere before the next check left Kodi showing it for up to 10 minutes).
  bool m_timersSoon = false;
  std::vector<epgstation::KodiChannel> m_channels;
  std::vector<epgstation::KodiGroup> m_groups;
  PVR_CONNECTION_STATE m_state = PVR_CONNECTION_STATE_UNKNOWN; // worker thread only
  // Last seen recordings fingerprint; a change makes Kodi reload recordings. Worker thread only.
  epgstation::RecordedSignature m_recordedSignature;
  std::string m_reservesFingerprint; // same for reservations (timers)
  // Near-future guide of all channels, refreshed every kEpgCheckEvery checks: a channel whose
  // programmes changed gets TriggerEpgUpdate (Kodi itself refreshes only every couple of hours).
  epgstation::ScheduleSnapshot m_epgSnapshot;
  int m_checksSinceEpgCheck = 0;
  static constexpr int kEpgCheckEvery = 10; // checks (kCheckSeconds each): 10 minutes
  static constexpr int kEpgWindowHours = 6;
  void CheckEpgChanges();
  void TimersChanged(); // TriggerTimerUpdate now and m_timersSoon
  int m_checksSinceTimerUpdate = 0; // times move without changing the fingerprint

  // The one recording Kodi is reading through the addon (Kodi plays one at a time).
  std::mutex m_streamMutex;
  std::unique_ptr<KodiRangeFile> m_streamSource;
  std::unique_ptr<epgstation::GrowingStream> m_stream;
  // Programme start/end of that recording; 0 = none. Atomic, not under m_streamMutex: Read
  // can block there for up to GrowingStream::kMaxWaitMs while caught up with the recorder.
  std::atomic<time_t> m_streamStart{0};
  // Set by CloseRecorded before it takes m_streamMutex: ends a Read that waits there.
  std::atomic<bool> m_streamCancel{false};
  std::atomic<time_t> m_streamEnd{0};

  // The live stream Kodi reads through the add-on (Kodi 22).
  std::mutex m_liveMutex;
  std::unique_ptr<KodiStreamFile> m_live;
  std::atomic<bool> m_liveOpen{false};

  std::mutex m_stopMutex;
  std::condition_variable m_stopCv;
  bool m_stop = false;
  std::thread m_worker; // last member: started after everything else is initialised
};
