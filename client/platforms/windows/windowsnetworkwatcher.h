/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WINDOWSNETWORKWATCHER_H
#define WINDOWSNETWORKWATCHER_H

#include <windows.h>
#include <wlanapi.h>
#include <powrprof.h>

#include "networkwatcherimpl.h"

class WindowsNetworkWatcher final : public NetworkWatcherImpl {
 public:
  WindowsNetworkWatcher(QObject* parent);
  struct NativeApi {
    decltype(&PowerRegisterSuspendResumeNotification) registerPower = &PowerRegisterSuspendResumeNotification;
    decltype(&PowerUnregisterSuspendResumeNotification) unregisterPower = &PowerUnregisterSuspendResumeNotification;
    decltype(&WlanOpenHandle) openWireless = &WlanOpenHandle;
    decltype(&WlanRegisterNotification) registerWireless = &WlanRegisterNotification;
    decltype(&WlanCloseHandle) closeWireless = &WlanCloseHandle;
  };
  WindowsNetworkWatcher(QObject* parent, const NativeApi &api);
  ~WindowsNetworkWatcher();

  void initialize() override;

 private:
  static void wlanCallback(PWLAN_NOTIFICATION_DATA data, PVOID context);
  static ULONG CALLBACK powerCallback(PVOID context, ULONG type, PVOID setting);

  void processWlan(PWLAN_NOTIFICATION_DATA data);

 private:
  // The handle is set during the initialization. Windows calls processWlan()
  // to inform about network changes.
  HANDLE m_wlanHandle = nullptr;
  HPOWERNOTIFY m_powerNotification = nullptr;
  DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS m_powerParameters {};
  NativeApi m_api;
  bool m_initialized = false;
  QString m_lastBSSID;
};

#endif  // WINDOWSNETWORKWATCHER_H
