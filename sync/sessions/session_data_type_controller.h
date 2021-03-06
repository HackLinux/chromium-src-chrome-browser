// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_SESSIONS_SESSION_DATA_TYPE_CONTROLLER_H_
#define CHROME_BROWSER_SYNC_SESSIONS_SESSION_DATA_TYPE_CONTROLLER_H_

#include "chrome/browser/sync/glue/local_device_info_provider.h"
#include "components/sync_driver/ui_data_type_controller.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class Profile;

namespace browser_sync {

class SyncedWindowDelegatesGetter;

// Overrides StartModels to avoid sync contention with sessions during
// a session restore operation at startup and to wait for the local
// device info to become available.
class SessionDataTypeController : public sync_driver::UIDataTypeController,
                                  public content::NotificationObserver {
 public:
  SessionDataTypeController(sync_driver::SyncApiComponentFactory* factory,
                            Profile* profile,
                            SyncedWindowDelegatesGetter* synced_window_getter,
                            LocalDeviceInfoProvider* local_device);

  // NotificationObserver interface.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 protected:
  virtual ~SessionDataTypeController();
  virtual bool StartModels() OVERRIDE;
  virtual void StopModels() OVERRIDE;

 private:
  bool IsWaiting();
  void MaybeCompleteLoading();
  void OnLocalDeviceInfoInitialized();

  Profile* const profile_;

  SyncedWindowDelegatesGetter* synced_window_getter_;
  content::NotificationRegistrar notification_registrar_;

  LocalDeviceInfoProvider* const local_device_;
  scoped_ptr<LocalDeviceInfoProvider::Subscription> subscription_;

  // Flags that indicate the reason for pending loading models.
  bool waiting_on_session_restore_;
  bool waiting_on_local_device_info_;

  DISALLOW_COPY_AND_ASSIGN(SessionDataTypeController);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_SESSIONS_SESSION_DATA_TYPE_CONTROLLER_H_

