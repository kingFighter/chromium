// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sync/sessions/nudge_tracker.h"

#include "base/basictypes.h"
#include "sync/internal_api/public/base/invalidation.h"
#include "sync/internal_api/public/sessions/sync_source_info.h"
#include "sync/protocol/sync.pb.h"

namespace syncer {
namespace sessions {

size_t NudgeTracker::kDefaultMaxPayloadsPerType = 10;

NudgeTracker::NudgeTracker()
    : updates_source_(sync_pb::GetUpdatesCallerInfo::UNKNOWN),
      invalidations_enabled_(false),
      invalidations_out_of_sync_(true) {
  ModelTypeSet protocol_types = ProtocolTypes();
  // Default initialize all the type trackers.
  for (ModelTypeSet::Iterator it = protocol_types.First(); it.Good();
       it.Inc()) {
    type_trackers_[it.Get()] = DataTypeTracker();
  }
}

NudgeTracker::~NudgeTracker() { }

bool NudgeTracker::IsSyncRequired() {
  for (TypeTrackerMap::iterator it = type_trackers_.begin();
       it != type_trackers_.end(); ++it) {
    if (it->second.IsSyncRequired()) {
      return true;
    }
  }
  return false;
}

void NudgeTracker::RecordSuccessfulSyncCycle() {
  updates_source_ = sync_pb::GetUpdatesCallerInfo::UNKNOWN;

  // A successful cycle while invalidations are enabled puts us back into sync.
  invalidations_out_of_sync_ = !invalidations_enabled_;

  for (TypeTrackerMap::iterator it = type_trackers_.begin();
       it != type_trackers_.end(); ++it) {
    it->second.RecordSuccessfulSyncCycle();
  }
}

void NudgeTracker::RecordLocalChange(ModelTypeSet types) {
  // Don't overwrite an NOTIFICATION or DATATYPE_REFRESH source.  The server
  // makes some assumptions about the source; overriding these sources with
  // LOCAL could lead to incorrect behaviour.  This is part of the reason why
  // we're deprecating 'source' in favor of 'origin'.
  if (updates_source_ != sync_pb::GetUpdatesCallerInfo::NOTIFICATION
      && updates_source_ != sync_pb::GetUpdatesCallerInfo::DATATYPE_REFRESH) {
    updates_source_ = sync_pb::GetUpdatesCallerInfo::LOCAL;
  }

  for (ModelTypeSet::Iterator it = types.First(); it.Good(); it.Inc()) {
    DCHECK(type_trackers_.find(it.Get()) != type_trackers_.end());
    type_trackers_[it.Get()].RecordLocalChange();
  }
}

void NudgeTracker::RecordLocalRefreshRequest(ModelTypeSet types) {
  // Don't overwrite an NOTIFICATION source.  The server makes some assumptions
  // about the source.  Overriding this source with LOCAL could lead to
  // incorrect behaviour.  This is part of the reason why we're deprecating
  // 'source' in favor of 'origin'.
  if (updates_source_ != sync_pb::GetUpdatesCallerInfo::NOTIFICATION) {
    updates_source_ = sync_pb::GetUpdatesCallerInfo::DATATYPE_REFRESH;
  }

  for (ModelTypeSet::Iterator it = types.First(); it.Good(); it.Inc()) {
    DCHECK(type_trackers_.find(it.Get()) != type_trackers_.end());
    type_trackers_[it.Get()].RecordLocalRefreshRequest();
  }
}

void NudgeTracker::RecordRemoteInvalidation(
    const ModelTypeInvalidationMap& invalidation_map) {
  updates_source_ = sync_pb::GetUpdatesCallerInfo::NOTIFICATION;

  for (ModelTypeInvalidationMap::const_iterator i = invalidation_map.begin();
       i != invalidation_map.end(); ++i) {
    const ModelType type = i->first;
    const std::string& payload = i->second.payload;
    DCHECK(type_trackers_.find(type) != type_trackers_.end());
    type_trackers_[type].RecordRemoteInvalidation(payload);
  }
}

void NudgeTracker::OnInvalidationsEnabled() {
  invalidations_enabled_ = true;
}

void NudgeTracker::OnInvalidationsDisabled() {
  invalidations_enabled_ = false;
  invalidations_out_of_sync_ = true;
}

void NudgeTracker::SetTypesThrottledUntil(
    ModelTypeSet types,
    base::TimeDelta length,
    base::TimeTicks now) {
  for (ModelTypeSet::Iterator it = types.First(); it.Good(); it.Inc()) {
    type_trackers_[it.Get()].ThrottleType(length, now);
  }
}

void NudgeTracker::UpdateTypeThrottlingState(base::TimeTicks now) {
  for (TypeTrackerMap::iterator it = type_trackers_.begin();
       it != type_trackers_.end(); ++it) {
    it->second.UpdateThrottleState(now);
  }
}

bool NudgeTracker::IsAnyTypeThrottled() const {
  for (TypeTrackerMap::const_iterator it = type_trackers_.begin();
       it != type_trackers_.end(); ++it) {
    if (it->second.IsThrottled()) {
      return true;
    }
  }
  return false;
}

bool NudgeTracker::IsTypeThrottled(ModelType type) const {
  DCHECK(type_trackers_.find(type) != type_trackers_.end());
  return type_trackers_.find(type)->second.IsThrottled();
}

base::TimeDelta NudgeTracker::GetTimeUntilNextUnthrottle(
    base::TimeTicks now) const {
  DCHECK(IsAnyTypeThrottled()) << "This function requires a pending unthrottle";
  const base::TimeDelta kMaxTimeDelta =
      base::TimeDelta::FromInternalValue(kint64max);

  // Return min of GetTimeUntilUnthrottle() values for all IsThrottled() types.
  base::TimeDelta time_until_next_unthrottle = kMaxTimeDelta;
  for (TypeTrackerMap::const_iterator it = type_trackers_.begin();
       it != type_trackers_.end(); ++it) {
    if (it->second.IsThrottled()) {
      time_until_next_unthrottle =
          std::min(time_until_next_unthrottle,
                   it->second.GetTimeUntilUnthrottle(now));
    }
  }
  DCHECK(kMaxTimeDelta != time_until_next_unthrottle);

  return time_until_next_unthrottle;
}

ModelTypeSet NudgeTracker::GetThrottledTypes() const {
  ModelTypeSet result;
  for (TypeTrackerMap::const_iterator it = type_trackers_.begin();
       it != type_trackers_.end(); ++it) {
    if (it->second.IsThrottled()) {
      result.Put(it->first);
    }
  }
  return result;
}

// This function is intended to mimic the behavior of older clients.  Newer
// clients and servers will not rely on SyncSourceInfo.  See FillProtoMessage
// for the more modern equivalent.
SyncSourceInfo NudgeTracker::GetSourceInfo() const {
  ModelTypeInvalidationMap invalidation_map;
  for (TypeTrackerMap::const_iterator it = type_trackers_.begin();
       it != type_trackers_.end(); ++it) {
    if (it->second.IsThrottled()) {
      // We pretend throttled types are not enabled by skipping them.
      continue;
    } else if (it->second.HasPendingInvalidation()) {
      // The old-style source info can contain only one hint per type.  We grab
      // the most recent, to mimic the old coalescing behaviour.
      Invalidation invalidation;
      invalidation.payload = it->second.GetMostRecentInvalidationPayload();
      invalidation_map.insert(std::make_pair(it->first, invalidation));
    } else if (it->second.HasLocalChangePending()) {
      // The old-style source info sent up an empty string (as opposed to
      // nothing at all) when the type was locally nudged, but had not received
      // any invalidations.
      Invalidation invalidation;
      invalidation.payload = "";
      invalidation_map.insert(std::make_pair(it->first, invalidation));
    }
  }

  return SyncSourceInfo(updates_source_, invalidation_map);
}

sync_pb::GetUpdatesCallerInfo::GetUpdatesSource NudgeTracker::updates_source()
    const {
  return updates_source_;
}

void NudgeTracker::FillProtoMessage(
    ModelType type,
    sync_pb::GetUpdateTriggers* msg) const {
  DCHECK(type_trackers_.find(type) != type_trackers_.end());

  // Fill what we can from the global data.
  msg->set_invalidations_out_of_sync(invalidations_out_of_sync_);

  // Delegate the type-specific work to the DataTypeTracker class.
  type_trackers_.find(type)->second.FillGetUpdatesTriggersMessage(msg);
}

void NudgeTracker::SetHintBufferSize(size_t size) {
  for (TypeTrackerMap::iterator it = type_trackers_.begin();
       it != type_trackers_.end(); ++it) {
    it->second.UpdatePayloadBufferSize(size);
  }
}

}  // namespace sessions
}  // namespace syncer
