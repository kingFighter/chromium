// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file defines specific implementation of BrowserDistribution class for
// Google Chrome.

#include "chrome/installer/util/google_chrome_distribution.h"

#include <windows.h>
#include <msi.h>

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/registry.h"
#include "base/win/windows_version.h"
#include "chrome/common/net/test_server_locations.h"
#include "chrome/installer/util/channel_info.h"
#include "chrome/installer/util/google_update_constants.h"
#include "chrome/installer/util/google_update_settings.h"
#include "chrome/installer/util/helper.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/l10n_string_util.h"
#include "chrome/installer/util/uninstall_metrics.h"
#include "chrome/installer/util/util_constants.h"
#include "chrome/installer/util/wmi.h"
#include "content/public/common/result_codes.h"

#include "installer_util_strings.h"  // NOLINT

namespace {

const wchar_t kChromeGuid[] = L"{8A69D345-D564-463c-AFF1-A69D9E530F96}";
const wchar_t kBrowserAppId[] = L"Chrome";
const wchar_t kCommandExecuteImplUuid[] =
    L"{5C65F4B0-3651-4514-B207-D10CB699B14B}";

// Substitute the locale parameter in uninstall URL with whatever
// Google Update tells us is the locale. In case we fail to find
// the locale, we use US English.
string16 LocalizeUrl(const wchar_t* url) {
  string16 language;
  if (!GoogleUpdateSettings::GetLanguage(&language))
    language = L"en-US";  // Default to US English.
  return ReplaceStringPlaceholders(url, language.c_str(), NULL);
}

string16 GetUninstallSurveyUrl() {
  const wchar_t kSurveyUrl[] = L"http://www.google.com/support/chrome/bin/"
                               L"request.py?hl=$1&contact_type=uninstall";
  return LocalizeUrl(kSurveyUrl);
}

}  // namespace

GoogleChromeDistribution::GoogleChromeDistribution()
    : BrowserDistribution(CHROME_BROWSER),
      product_guid_(kChromeGuid) {
}

void GoogleChromeDistribution::DoPostUninstallOperations(
    const Version& version,
    const base::FilePath& local_data_path,
    const string16& distribution_data) {
  // Send the Chrome version and OS version as params to the form.
  // It would be nice to send the locale, too, but I don't see an
  // easy way to get that in the existing code. It's something we
  // can add later, if needed.
  // We depend on installed_version.GetString() not having spaces or other
  // characters that need escaping: 0.2.13.4. Should that change, we will
  // need to escape the string before using it in a URL.
  const string16 kVersionParam = L"crversion";
  const string16 kOSParam = L"os";
  base::win::OSInfo::VersionNumber version_number =
      base::win::OSInfo::GetInstance()->version_number();
  string16 os_version = base::StringPrintf(L"%d.%d.%d",
      version_number.major, version_number.minor, version_number.build);

  base::FilePath iexplore;
  if (!PathService::Get(base::DIR_PROGRAM_FILES, &iexplore))
    return;

  iexplore = iexplore.AppendASCII("Internet Explorer");
  iexplore = iexplore.AppendASCII("iexplore.exe");

  string16 command = iexplore.value() + L" " + GetUninstallSurveyUrl() +
      L"&" + kVersionParam + L"=" + UTF8ToWide(version.GetString()) + L"&" +
      kOSParam + L"=" + os_version;

  string16 uninstall_metrics;
  if (installer::ExtractUninstallMetricsFromFile(local_data_path,
                                                 &uninstall_metrics)) {
    // The user has opted into anonymous usage data collection, so append
    // metrics and distribution data.
    command += uninstall_metrics;
    if (!distribution_data.empty()) {
      command += L"&";
      command += distribution_data;
    }
  }

  int pid = 0;
  // The reason we use WMI to launch the process is because the uninstall
  // process runs inside a Job object controlled by the shell. As long as there
  // are processes running, the shell will not close the uninstall applet. WMI
  // allows us to escape from the Job object so the applet will close.
  installer::WMIProcess::Launch(command, &pid);
}

string16 GoogleChromeDistribution::GetActiveSetupGuid() {
  return product_guid();
}

string16 GoogleChromeDistribution::GetAppGuid() {
  return product_guid();
}

string16 GoogleChromeDistribution::GetBaseAppName() {
  // I'd really like to return L ## PRODUCT_FULLNAME_STRING; but that's no good
  // since it'd be "Chromium" in a non-Chrome build, which isn't at all what I
  // want.  Sigh.
  return L"Google Chrome";
}

string16 GoogleChromeDistribution::GetAppShortCutName() {
  const string16& app_shortcut_name =
      installer::GetLocalizedString(IDS_PRODUCT_NAME_BASE);
  return app_shortcut_name;
}

string16 GoogleChromeDistribution::GetAlternateApplicationName() {
  const string16& alt_product_name =
      installer::GetLocalizedString(IDS_OEM_MAIN_SHORTCUT_NAME_BASE);
  return alt_product_name;
}

string16 GoogleChromeDistribution::GetBaseAppId() {
  return kBrowserAppId;
}

string16 GoogleChromeDistribution::GetInstallSubDir() {
  string16 sub_dir(installer::kGoogleChromeInstallSubDir1);
  sub_dir.append(L"\\");
  sub_dir.append(installer::kGoogleChromeInstallSubDir2);
  return sub_dir;
}

string16 GoogleChromeDistribution::GetPublisherName() {
  const string16& publisher_name =
      installer::GetLocalizedString(IDS_ABOUT_VERSION_COMPANY_NAME_BASE);
  return publisher_name;
}

string16 GoogleChromeDistribution::GetAppDescription() {
  const string16& app_description =
      installer::GetLocalizedString(IDS_SHORTCUT_TOOLTIP_BASE);
  return app_description;
}

std::string GoogleChromeDistribution::GetSafeBrowsingName() {
  return "googlechrome";
}

string16 GoogleChromeDistribution::GetStateKey() {
  string16 key(google_update::kRegPathClientState);
  key.append(L"\\");
  key.append(product_guid());
  return key;
}

string16 GoogleChromeDistribution::GetStateMediumKey() {
  string16 key(google_update::kRegPathClientStateMedium);
  key.append(L"\\");
  key.append(product_guid());
  return key;
}

std::string GoogleChromeDistribution::GetNetworkStatsServer() const {
  return chrome_common_net::kEchoTestServerLocation;
}

std::string GoogleChromeDistribution::GetHttpPipeliningTestServer() const {
  return chrome_common_net::kPipelineTestServerBaseUrl;
}

string16 GoogleChromeDistribution::GetDistributionData(HKEY root_key) {
  string16 sub_key(google_update::kRegPathClientState);
  sub_key.append(L"\\");
  sub_key.append(product_guid());

  base::win::RegKey client_state_key(root_key, sub_key.c_str(), KEY_READ);
  string16 result;
  string16 brand_value;
  if (client_state_key.ReadValue(google_update::kRegRLZBrandField,
                                 &brand_value) == ERROR_SUCCESS) {
    result = google_update::kRegRLZBrandField;
    result.append(L"=");
    result.append(brand_value);
    result.append(L"&");
  }

  string16 client_value;
  if (client_state_key.ReadValue(google_update::kRegClientField,
                                 &client_value) == ERROR_SUCCESS) {
    result.append(google_update::kRegClientField);
    result.append(L"=");
    result.append(client_value);
    result.append(L"&");
  }

  string16 ap_value;
  // If we fail to read the ap key, send up "&ap=" anyway to indicate
  // that this was probably a stable channel release.
  client_state_key.ReadValue(google_update::kRegApField, &ap_value);
  result.append(google_update::kRegApField);
  result.append(L"=");
  result.append(ap_value);

  return result;
}

string16 GoogleChromeDistribution::GetUninstallLinkName() {
  const string16& link_name =
      installer::GetLocalizedString(IDS_UNINSTALL_CHROME_BASE);
  return link_name;
}

string16 GoogleChromeDistribution::GetUninstallRegPath() {
  return L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
         L"Google Chrome";
}

string16 GoogleChromeDistribution::GetVersionKey() {
  string16 key(google_update::kRegPathClients);
  key.append(L"\\");
  key.append(product_guid());
  return key;
}

string16 GoogleChromeDistribution::GetIconFilename() {
  return installer::kChromeExe;
}

bool GoogleChromeDistribution::GetCommandExecuteImplClsid(
    string16* handler_class_uuid) {
  if (handler_class_uuid)
    *handler_class_uuid = kCommandExecuteImplUuid;
  return true;
}

bool GoogleChromeDistribution::AppHostIsSupported() {
  return true;
}

// This method checks if we need to change "ap" key in Google Update to try
// full installer as fall back method in case incremental installer fails.
// - If incremental installer fails we append a magic string ("-full"), if
// it is not present already, so that Google Update server next time will send
// full installer to update Chrome on the local machine
// - If we are currently running full installer, we remove this magic
// string (if it is present) regardless of whether installer failed or not.
// There is no fall-back for full installer :)
void GoogleChromeDistribution::UpdateInstallStatus(bool system_install,
    installer::ArchiveType archive_type,
    installer::InstallStatus install_status) {
  GoogleUpdateSettings::UpdateInstallStatus(system_install,
      archive_type, InstallUtil::GetInstallReturnCode(install_status),
      product_guid());
}

bool GoogleChromeDistribution::ShouldSetExperimentLabels() {
  return true;
}

bool GoogleChromeDistribution::HasUserExperiments() {
  return true;
}
