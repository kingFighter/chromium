// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/browser/extensions/platform_app_browsertest_util.h"

// Supported on all platforms, but on Windows only if RLZ is enabled.
#if !defined(OS_WIN) || defined(ENABLE_RLZ)

class MusicManagerPrivateTest : public extensions::PlatformAppBrowserTest {
};

IN_PROC_BROWSER_TEST_F(MusicManagerPrivateTest, DeviceIdValueReturned) {
  ASSERT_TRUE(RunPlatformAppTest(
      "platform_apps/music_manager_private/device_id_value_returned"))
          << message_;
}

#endif
