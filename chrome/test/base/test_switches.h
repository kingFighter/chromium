// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_TEST_BASE_TEST_SWITCHES_H_
#define CHROME_TEST_BASE_TEST_SWITCHES_H_

#include "build/build_config.h"

namespace switches {

// All switches in alphabetical order. The switches should be documented
// alongside the definition of their values in the .cc file.
extern const char kAlsoEmitSuccessLogs[];
extern const char kExtraChromeFlags[];
extern const char kEnableChromiumBranding[];
extern const char kEnableErrorDialogs[];
extern const char kPageCyclerIterations[];
extern const char kTestingChannel[];

#if defined(OS_WIN) && defined(USE_AURA)
extern const char kAshBrowserTests[];
#endif

}  // namespace switches

#endif  // CHROME_TEST_BASE_TEST_SWITCHES_H_
