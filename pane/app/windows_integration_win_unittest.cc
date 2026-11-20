// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/windows_integration.h"

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_path_override.h"
#include "base/test/test_reg_util_win.h"
#include "base/time/time.h"
#include "base/win/registry.h"
#include "base/win/scoped_com_initializer.h"
#include "base/win/shortcut.h"
#include "chrome/install_static/install_util.h"
#include "chrome/install_static/test/scoped_install_details.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/l10n_string_util.h"
#include "chrome/installer/util/registry_entry.h"
#include "chrome/installer/util/shell_util.h"
#include "pane/model/app_settings.h"
#include "pane/model/data_lock.h"
#include "pane/model/persisted_file.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::windows {

namespace {

// HKCU, the shell's folders, and the app's own (its version beside it) in
// the test's: nothing of the computer's own shortcuts or registry.
class WindowsIntegrationTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_.CreateUniqueTempDir());
    ASSERT_NO_FATAL_FAILURE(registry_.OverrideRegistry(HKEY_CURRENT_USER));
    for (const int key : {static_cast<int>(base::DIR_USER_DESKTOP),
                          static_cast<int>(base::DIR_START_MENU),
                          static_cast<int>(base::DIR_TASKBAR_PINS),
                          static_cast<int>(base::DIR_USER_QUICK_LAUNCH),
                          static_cast<int>(base::DIR_APP_SHORTCUTS),
                          static_cast<int>(base::DIR_USER_STARTUP)}) {
      const base::FilePath folder = temp_.GetPath().AppendASCII(
          base::StrCat({"shell-", base::NumberToString(key)}));
      ASSERT_TRUE(base::CreateDirectory(folder));
      overrides_.push_back(
          std::make_unique<base::ScopedPathOverride>(key, folder));
    }
    app_ = temp_.GetPath().Append(L"Programs\\Pane\\Application");
    ASSERT_TRUE(base::CreateDirectory(app_));
    chrome_exe_ = app_.Append(L"chrome.exe");
    ASSERT_TRUE(base::WriteFile(chrome_exe_, "MZ"));
    for (const wchar_t* icon :
         {L"Pane.ico", L"AppIcon-Day.ico", L"AppIcon-Night.ico",
          L"AppIcon-Sunset.ico", L"PaneHTM.ico", L"PanePDF.ico"}) {
      ASSERT_TRUE(base::WriteFile(app_.Append(icon), "ico"));
    }
    ASSERT_TRUE(base::WriteFile(app_.Append(L"pane-version"), "0.21.0"));
    overrides_.push_back(
        std::make_unique<base::ScopedPathOverride>(base::DIR_ASSETS, app_));
    data_ = temp_.GetPath().Append(L"Pane");
    UsePanesWords();
  }

  void TearDown() override { installer::SetTranslationDelegate(nullptr); }

  base::FilePath Shortcut(int folder) const {
    base::FilePath path;
    base::PathService::Get(folder, &path);
    return path.Append(L"Pane.lnk");
  }

  std::wstring Read(const std::wstring& key, const wchar_t* name) const {
    std::wstring value;
    base::win::RegKey(HKEY_CURRENT_USER, key.c_str(), KEY_READ)
        .ReadValue(name, &value);
    return value;
  }

  bool KeyExists(const std::wstring& key) const {
    return base::win::RegKey(HKEY_CURRENT_USER, key.c_str(), KEY_READ).Valid();
  }

  base::win::ScopedCOMInitializer com_;
  // Pane's install mode for a per-user install, as chrome_elf gives
  // chrome.exe its own.
  install_static::ScopedInstallDetails install_details_;
  base::ScopedTempDir temp_;
  registry_util::RegistryOverrideManager registry_;
  std::vector<std::unique_ptr<base::ScopedPathOverride>> overrides_;
  base::FilePath app_;
  base::FilePath chrome_exe_;
  base::FilePath data_;
};

}  // namespace

// Pane's words where Chromium's installer strings reach Windows (11.12.3,
// 11.12.5).
TEST_F(WindowsIntegrationTest, PanesWords) {
  EXPECT_EQ(InstallUtil::GetShortcutName(), L"Pane");
  EXPECT_EQ(InstallUtil::GetAppDescription(),
            L"Every profile, its own internet.");
  EXPECT_EQ(InstallUtil::GetLongAppDescription(),
            L"Every profile, its own internet: its own sign-ins, proxy, "
            L"fingerprint, and extensions.");
  EXPECT_EQ(InstallUtil::GetPublisherName(), L"Rounded Corners");
}

// Each sky its own file (11.12.16).
TEST_F(WindowsIntegrationTest, SkyIcons) {
  EXPECT_EQ(SkyIcon(app_, model::AppIcon::kSunset),
            app_.Append(L"AppIcon-Sunset.ico"));
  EXPECT_EQ(SkyIcon(app_, model::AppIcon::kDay),
            app_.Append(L"AppIcon-Day.ico"));
}

// The setting, and Automatic by Pane's Theme or, under System, Windows'.
TEST_F(WindowsIntegrationTest, ShortcutSkyFollowsTheSettings) {
  base::win::RegKey personalize;
  ASSERT_EQ(personalize.Create(HKEY_CURRENT_USER,
                               L"Software\\Microsoft\\Windows\\"
                               L"CurrentVersion\\Themes\\Personalize",
                               KEY_SET_VALUE),
            ERROR_SUCCESS);
  ASSERT_EQ(personalize.WriteValue(L"AppsUseLightTheme", DWORD{1}),
            ERROR_SUCCESS);
  EXPECT_EQ(ShortcutSky(data_), model::AppIcon::kDay);
  ASSERT_EQ(personalize.WriteValue(L"AppsUseLightTheme", DWORD{0}),
            ERROR_SUCCESS);
  EXPECT_EQ(ShortcutSky(data_), model::AppIcon::kNight);
  // settings.json as Pane writes it.
  model::AppSettings sunset;
  sunset.theme = model::Theme::kLight;
  sunset.app_icon = model::AppIcon::kSunset;
  ASSERT_TRUE(model::PersistedFile(data_, "settings")
                  .Save(model::SettingsState(sunset)));
  EXPECT_EQ(ShortcutSky(data_), model::AppIcon::kSunset);
}

// Default apps' entries on Chromium's model, Pane's names and icons, and no
// App Paths (11.12.5).
TEST_F(WindowsIntegrationTest, DefaultAppsEntriesArePanes) {
  ASSERT_TRUE(ShellUtil::AddRegistryEntries(HKEY_CURRENT_USER,
                                            DefaultAppsEntries(chrome_exe_)));
  EXPECT_EQ(Read(L"Software\\Classes\\PaneHTM", L""), L"Pane HTML Document");
  EXPECT_EQ(Read(L"Software\\Classes\\PaneHTM\\DefaultIcon", L""),
            app_.Append(L"PaneHTM.ico").value() + L",0");
  EXPECT_EQ(Read(L"Software\\Classes\\PanePDF\\DefaultIcon", L""),
            app_.Append(L"PanePDF.ico").value() + L",0");
  const std::wstring capabilities =
      L"Software\\Clients\\StartMenuInternet\\Pane\\Capabilities";
  EXPECT_EQ(Read(capabilities, L"ApplicationName"), L"Pane");
  EXPECT_EQ(Read(capabilities, L"ApplicationIcon"),
            app_.Append(L"Pane.ico").value() + L",0");
  EXPECT_EQ(Read(capabilities, L"ApplicationDescription"),
            L"Every profile, its own internet: its own sign-ins, proxy, "
            L"fingerprint, and extensions.");
  EXPECT_EQ(Read(capabilities + L"\\URLAssociations", L"https"), L"PaneHTM");
  EXPECT_EQ(Read(capabilities + L"\\FileAssociations", L".pdf"), L"PanePDF");
  // Pane opens the web's pages and files alone: no mailto or other scheme.
  EXPECT_EQ(Read(capabilities + L"\\URLAssociations", L"mailto"), L"");
  EXPECT_FALSE(KeyExists(
      L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe"));
}

// Start's shortcut alone, as Register makes it, in a Start folder not there
// yet, as a test copy's home has none at its first launch: what Chrome reads
// to show its toasts through Windows (commit dde4090). Nothing else is
// registered: no desktop shortcut, toast activator, or uninstall entry.
TEST_F(WindowsIntegrationTest, MakesStartsShortcutAsTheInstallDoes) {
  base::FilePath start_menu;
  ASSERT_TRUE(base::PathService::Get(base::DIR_START_MENU, &start_menu));
  ASSERT_TRUE(base::DeletePathRecursively(start_menu));
  EXPECT_FALSE(InstallUtil::IsStartMenuShortcutWithActivatorGuidInstalled());

  ASSERT_TRUE(MakeStartShortcut(chrome_exe_, data_));

  base::win::ShortcutProperties found;
  ASSERT_TRUE(base::win::ResolveShortcutProperties(
      Shortcut(base::DIR_START_MENU),
      base::win::ShortcutProperties::PROPERTIES_ALL, &found));
  EXPECT_TRUE(base::FilePath::CompareEqualIgnoreCase(found.target.value(),
                                                     chrome_exe_.value()));
  EXPECT_TRUE(base::FilePath::CompareEqualIgnoreCase(
      found.icon.value(), SkyIcon(app_, ShortcutSky(data_)).value()));
  EXPECT_EQ(found.description, L"Every profile, its own internet.");
  EXPECT_EQ(found.app_id,
            ShellUtil::GetBrowserModelId(InstallUtil::IsPerUserInstall()));
  EXPECT_EQ(found.toast_activator_clsid,
            install_static::GetToastActivatorClsid());
  EXPECT_TRUE(InstallUtil::IsStartMenuShortcutWithActivatorGuidInstalled());
  EXPECT_FALSE(base::PathExists(Shortcut(base::DIR_USER_DESKTOP)));
  EXPECT_FALSE(KeyExists(InstallUtil::GetToastActivatorRegistryPath()));
  EXPECT_FALSE(KeyExists(install_static::GetUninstallRegistryPath()));
}

// Start's shortcut, the desktop's at a first install, Default apps, the
// uninstall entry, the toast activator; a repair puts Start's back and never
// the desktop's (11.12.3's A); the uninstall takes them all and keeps the
// data's preference MACs with the data (11.12.9's B).
TEST_F(WindowsIntegrationTest, RegistersAndUnregisters) {
  ASSERT_EQ(Register(chrome_exe_, data_, /*first_install=*/true),
            static_cast<DWORD>(ERROR_SUCCESS));
  const base::FilePath start = Shortcut(base::DIR_START_MENU);
  const base::FilePath desktop = Shortcut(base::DIR_USER_DESKTOP);
  for (const base::FilePath& shortcut : {start, desktop}) {
    base::win::ShortcutProperties found;
    ASSERT_TRUE(base::win::ResolveShortcutProperties(
        shortcut, base::win::ShortcutProperties::PROPERTIES_ALL, &found))
        << shortcut;
    EXPECT_TRUE(base::FilePath::CompareEqualIgnoreCase(found.target.value(),
                                                       chrome_exe_.value()));
    EXPECT_EQ(found.icon.BaseName().value().rfind(L"AppIcon-", 0), 0u);
    EXPECT_EQ(found.description, L"Every profile, its own internet.");
    EXPECT_EQ(found.app_id,
              ShellUtil::GetBrowserModelId(InstallUtil::IsPerUserInstall()));
    EXPECT_EQ(found.toast_activator_clsid,
              install_static::GetToastActivatorClsid());
  }
  const std::wstring uninstall = install_static::GetUninstallRegistryPath();
  EXPECT_EQ(Read(uninstall, L"DisplayName"), L"Pane");
  EXPECT_EQ(Read(uninstall, L"Publisher"), L"Rounded Corners");
  EXPECT_EQ(Read(uninstall, L"DisplayVersion"), L"0.21.0");
  EXPECT_EQ(Read(uninstall, L"DisplayIcon"), app_.Append(L"Pane.ico").value());
  EXPECT_EQ(Read(uninstall, L"UninstallString"),
            base::StrCat({L"\"", chrome_exe_.value(), L"\" --pane-uninstall"}));
  EXPECT_EQ(
      Read(InstallUtil::GetToastActivatorRegistryPath() + L"\\LocalServer32",
           L"ServerExecutable"),
      app_.Append(L"notification_helper.exe").value());
  EXPECT_EQ(Read(L"Software\\RegisteredApplications", L"Pane"),
            L"Software\\Clients\\StartMenuInternet\\Pane\\Capabilities");

  // A repair after the user deleted both.
  ASSERT_TRUE(base::DeleteFile(start));
  ASSERT_TRUE(base::DeleteFile(desktop));
  ASSERT_EQ(Register(chrome_exe_, data_, /*first_install=*/false),
            static_cast<DWORD>(ERROR_SUCCESS));
  EXPECT_TRUE(base::PathExists(start));
  EXPECT_FALSE(base::PathExists(desktop));

  // The data kept: its preference MACs stay, the rest of Chromium's key goes.
  ASSERT_TRUE(base::CreateDirectory(data_));
  ASSERT_TRUE(base::WriteFile(data_.Append(L"profiles.json"), "{}"));
  const std::wstring own = install_static::GetRegistryPath();
  base::win::RegKey macs;
  ASSERT_EQ(macs.Create(HKEY_CURRENT_USER, (own + L"\\PreferenceMACs").c_str(),
                        KEY_SET_VALUE),
            ERROR_SUCCESS);
  ASSERT_EQ(macs.WriteValue(L"mac", L"kept"), ERROR_SUCCESS);
  base::win::RegKey metrics;
  ASSERT_EQ(metrics.Create(HKEY_CURRENT_USER, own.c_str(), KEY_SET_VALUE),
            ERROR_SUCCESS);
  ASSERT_EQ(metrics.WriteValue(L"metricsid", L"x"), ERROR_SUCCESS);
  ASSERT_EQ(Unregister(chrome_exe_, data_, /*erase=*/false, EraseProgress()),
            static_cast<DWORD>(ERROR_SUCCESS));
  EXPECT_FALSE(base::PathExists(start));
  EXPECT_FALSE(KeyExists(uninstall));
  EXPECT_FALSE(KeyExists(L"Software\\Classes\\PaneHTM"));
  EXPECT_FALSE(KeyExists(L"Software\\Clients\\StartMenuInternet\\Pane"));
  EXPECT_FALSE(KeyExists(InstallUtil::GetToastActivatorRegistryPath()));
  EXPECT_EQ(Read(L"Software\\RegisteredApplications", L"Pane"), L"");
  EXPECT_EQ(Read(own + L"\\PreferenceMACs", L"mac"), L"kept");
  EXPECT_EQ(Read(own, L"metricsid"), L"");
  EXPECT_TRUE(base::PathExists(data_.Append(L"profiles.json")));
}

// After an update's swap the new version registers as a reinstall's repair
// does (pane/installer/swap.h): Installed apps' entry names its version and
// its size, and the day, as Chrome's installer writes InstallDate at every
// update (install_worker.cc's AddUninstallShortcutWorkItems); Start's
// shortcut is made again, the desktop's the user deleted stays deleted, and
// their pin is left as it was.
TEST_F(WindowsIntegrationTest, AnUpdatesRepairNamesTheNewVersion) {
  ASSERT_EQ(Register(chrome_exe_, data_, /*first_install=*/true),
            static_cast<DWORD>(ERROR_SUCCESS));
  const std::wstring uninstall = install_static::GetUninstallRegistryPath();
  // The user's since: Pane pinned, the desktop's shortcut deleted; and the
  // entry dated long before, to see the day written again.
  base::FilePath pins;
  ASSERT_TRUE(base::PathService::Get(base::DIR_TASKBAR_PINS, &pins));
  const base::FilePath pin = pins.Append(L"Pane.lnk");
  base::win::ShortcutProperties pinned;
  pinned.set_target(chrome_exe_);
  ASSERT_TRUE(base::win::CreateOrUpdateShortcutLink(
      pin, pinned, base::win::ShortcutOperation::kCreateAlways));
  std::string pin_before;
  ASSERT_TRUE(base::ReadFileToString(pin, &pin_before));
  ASSERT_TRUE(base::DeleteFile(Shortcut(base::DIR_USER_DESKTOP)));
  ASSERT_EQ(
      base::win::RegKey(HKEY_CURRENT_USER, uninstall.c_str(), KEY_SET_VALUE)
          .WriteValue(L"InstallDate", L"20200101"),
      ERROR_SUCCESS);

  // The next version swapped in: its own version, and a larger library.
  ASSERT_TRUE(base::WriteFile(app_.Append(L"pane-version"), "0.21.1"));
  ASSERT_TRUE(base::WriteFile(app_.Append(L"chrome.dll"),
                              std::string(256 * 1024, 'x')));
  ASSERT_EQ(Register(chrome_exe_, data_, /*first_install=*/false),
            static_cast<DWORD>(ERROR_SUCCESS));

  EXPECT_EQ(Read(uninstall, L"DisplayVersion"), L"0.21.1");
  DWORD size = 0;
  EXPECT_EQ(base::win::RegKey(HKEY_CURRENT_USER, uninstall.c_str(), KEY_READ)
                .ReadValueDW(L"EstimatedSize", &size),
            ERROR_SUCCESS);
  EXPECT_EQ(size, static_cast<DWORD>(base::ComputeDirectorySize(app_) / 1024));
  EXPECT_GE(size, 256u);
  base::Time::Exploded today;
  base::Time::Now().LocalExplode(&today);
  EXPECT_EQ(Read(uninstall, L"InstallDate"),
            base::ASCIIToWide(base::StringPrintf(
                "%04d%02d%02d", today.year, today.month, today.day_of_month)));
  EXPECT_TRUE(base::PathExists(Shortcut(base::DIR_START_MENU)));
  EXPECT_FALSE(base::PathExists(Shortcut(base::DIR_USER_DESKTOP)));
  std::string pin_after;
  ASSERT_TRUE(base::ReadFileToString(pin, &pin_after));
  EXPECT_EQ(pin_after, pin_before);
}

// With the box ticked: the data erased, the wipe's way, and Chromium's key
// gone whole; the bar hears it through.
TEST_F(WindowsIntegrationTest, UnregistersErasingTheData) {
  ASSERT_EQ(Register(chrome_exe_, data_, /*first_install=*/true),
            static_cast<DWORD>(ERROR_SUCCESS));
  ASSERT_TRUE(base::CreateDirectory(data_.Append(L"User Data\\abc")));
  ASSERT_TRUE(base::WriteFile(data_.Append(L"security.json"), "{}"));
  ASSERT_TRUE(base::WriteFile(data_.Append(L"User Data\\abc\\Cookies"),
                              std::string(4096, 'c')));
  base::win::RegKey macs;
  ASSERT_EQ(
      macs.Create(
          HKEY_CURRENT_USER,
          (install_static::GetRegistryPath() + L"\\PreferenceMACs").c_str(),
          KEY_SET_VALUE),
      ERROR_SUCCESS);
  uint64_t last = 0;
  uint64_t total = 0;
  ASSERT_EQ(Unregister(chrome_exe_, data_, /*erase=*/true,
                       base::BindRepeating(
                           [](uint64_t* last, uint64_t* total, uint64_t erased,
                              uint64_t all) {
                             *last = erased;
                             *total = all;
                           },
                           base::Unretained(&last), base::Unretained(&total))),
            static_cast<DWORD>(ERROR_SUCCESS));
  EXPECT_FALSE(base::PathExists(data_));
  EXPECT_FALSE(KeyExists(install_static::GetRegistryPath()));
  EXPECT_EQ(total, 4096u + 2u);
  EXPECT_EQ(last, total);
}

// With no data to keep, as where its user deleted the folder: Chromium's key
// goes whole, its preference MACs with it, which named nothing (commit
// f475f04).
TEST_F(WindowsIntegrationTest, UnregistersWithNoDataLeavingNoMacs) {
  ASSERT_EQ(Register(chrome_exe_, data_, /*first_install=*/true),
            static_cast<DWORD>(ERROR_SUCCESS));
  ASSERT_FALSE(base::PathExists(data_));
  const std::wstring own = install_static::GetRegistryPath();
  base::win::RegKey macs;
  ASSERT_EQ(macs.Create(HKEY_CURRENT_USER, (own + L"\\PreferenceMACs").c_str(),
                        KEY_SET_VALUE),
            ERROR_SUCCESS);
  ASSERT_EQ(macs.WriteValue(L"mac", L"orphaned"), ERROR_SUCCESS);
  macs.Close();
  ASSERT_EQ(Unregister(chrome_exe_, data_, /*erase=*/false, EraseProgress()),
            static_cast<DWORD>(ERROR_SUCCESS));
  EXPECT_FALSE(KeyExists(own));
  EXPECT_FALSE(KeyExists(install_static::GetUninstallRegistryPath()));
  EXPECT_FALSE(base::PathExists(data_));
}

// A step that fails leaves Installed apps' entry, so the uninstall runs
// again from there, the app folder untouched (pane/installer/removal.h); once
// everything else has gone, the entry goes last.
TEST_F(WindowsIntegrationTest, KeepsTheEntryWhileAStepFails) {
  ASSERT_EQ(Register(chrome_exe_, data_, /*first_install=*/true),
            static_cast<DWORD>(ERROR_SUCCESS));
  ASSERT_TRUE(base::CreateDirectory(data_.Append(L"User Data")));
  const base::FilePath kept = data_.Append(L"User Data\\Local State");
  ASSERT_TRUE(base::WriteFile(kept, "{}"));
  // Held by another program, which doesn't share its deletion.
  base::File held(kept, base::File::FLAG_OPEN | base::File::FLAG_READ);
  ASSERT_TRUE(held.IsValid());
  EXPECT_NE(Unregister(chrome_exe_, data_, /*erase=*/true, EraseProgress()),
            static_cast<DWORD>(ERROR_SUCCESS));
  EXPECT_TRUE(KeyExists(install_static::GetUninstallRegistryPath()));
  EXPECT_FALSE(base::PathExists(Shortcut(base::DIR_START_MENU)));
  EXPECT_FALSE(KeyExists(L"Software\\Classes\\PaneHTM"));

  held.Close();
  EXPECT_EQ(Unregister(chrome_exe_, data_, /*erase=*/true, EraseProgress()),
            static_cast<DWORD>(ERROR_SUCCESS));
  EXPECT_FALSE(KeyExists(install_static::GetUninstallRegistryPath()));
  EXPECT_FALSE(base::PathExists(data_));
}

// A Pane on the data: nothing changes.
TEST_F(WindowsIntegrationTest, NothingWhilePaneHoldsTheData) {
  ASSERT_EQ(Register(chrome_exe_, data_, /*first_install=*/true),
            static_cast<DWORD>(ERROR_SUCCESS));
  ASSERT_TRUE(base::CreateDirectory(data_));
  std::unique_ptr<model::DataLock> held = model::DataLock::Take(data_);
  ASSERT_TRUE(held);
  EXPECT_EQ(Unregister(chrome_exe_, data_, /*erase=*/true, EraseProgress()),
            kDataInUse);
  EXPECT_TRUE(base::PathExists(Shortcut(base::DIR_START_MENU)));
  EXPECT_TRUE(KeyExists(install_static::GetUninstallRegistryPath()));
}

// The shortcuts follow the App icon (11.12.16): Pane's own and its pins, never
// another app's.
TEST_F(WindowsIntegrationTest, ShortcutsFollowTheAppIcon) {
  ASSERT_EQ(Register(chrome_exe_, data_, /*first_install=*/true),
            static_cast<DWORD>(ERROR_SUCCESS));
  base::FilePath pins;
  ASSERT_TRUE(base::PathService::Get(base::DIR_TASKBAR_PINS, &pins));
  base::win::ShortcutProperties pin;
  pin.set_target(chrome_exe_);
  pin.set_icon(app_.Append(L"Pane.ico"), 0);
  ASSERT_TRUE(base::win::CreateOrUpdateShortcutLink(
      pins.Append(L"Pane.lnk"), pin,
      base::win::ShortcutOperation::kCreateAlways));
  const base::FilePath other_exe = temp_.GetPath().Append(L"other.exe");
  ASSERT_TRUE(base::WriteFile(other_exe, "MZ"));
  base::win::ShortcutProperties other;
  other.set_target(other_exe);
  other.set_icon(other_exe, 0);
  ASSERT_TRUE(base::win::CreateOrUpdateShortcutLink(
      pins.Append(L"Other.lnk"), other,
      base::win::ShortcutOperation::kCreateAlways));

  PointedShortcuts pointed =
      PointShortcutsAt(chrome_exe_, model::AppIcon::kSunset);
  EXPECT_EQ(pointed.rewritten, 3);
  EXPECT_EQ(pointed.failed, 0);
  for (const base::FilePath& shortcut :
       {Shortcut(base::DIR_START_MENU), Shortcut(base::DIR_USER_DESKTOP),
        pins.Append(L"Pane.lnk")}) {
    base::win::ShortcutProperties found;
    ASSERT_TRUE(base::win::ResolveShortcutProperties(
        shortcut, base::win::ShortcutProperties::PROPERTIES_ICON, &found));
    EXPECT_EQ(found.icon, app_.Append(L"AppIcon-Sunset.ico")) << shortcut;
  }
  base::win::ShortcutProperties found;
  ASSERT_TRUE(base::win::ResolveShortcutProperties(
      pins.Append(L"Other.lnk"), base::win::ShortcutProperties::PROPERTIES_ICON,
      &found));
  EXPECT_EQ(found.icon, other_exe);
  // Already pointing there: nothing written.
  pointed = PointShortcutsAt(chrome_exe_, model::AppIcon::kSunset);
  EXPECT_EQ(pointed.rewritten, 0);
  EXPECT_EQ(pointed.failed, 0);
}

// A shortcut another program holds mapped, as Windows' shell maps Start's
// while it reads it for the taskbar's button, can't be rewritten
// (ERROR_USER_MAPPED_FILE): that one counts as failed, the others are
// rewritten, and once it is let go the next pointing rewrites it (ADR 0089).
TEST_F(WindowsIntegrationTest, AShortcutHeldMappedIsReportedAndRewrittenLater) {
  ASSERT_EQ(Register(chrome_exe_, data_, /*first_install=*/true),
            static_cast<DWORD>(ERROR_SUCCESS));
  const base::FilePath start = Shortcut(base::DIR_START_MENU);
  base::File file(start, base::File::FLAG_OPEN | base::File::FLAG_READ |
                             base::File::FLAG_WIN_SHARE_DELETE);
  ASSERT_TRUE(file.IsValid());
  HANDLE mapping = ::CreateFileMappingW(file.GetPlatformFile(), nullptr,
                                        PAGE_READONLY, 0, 0, nullptr);
  ASSERT_TRUE(mapping);
  const void* view = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  ASSERT_TRUE(view);

  PointedShortcuts pointed =
      PointShortcutsAt(chrome_exe_, model::AppIcon::kSunset);
  EXPECT_EQ(pointed.rewritten, 1);  // The desktop's.
  EXPECT_EQ(pointed.failed, 1);     // Start's, held.

  ::UnmapViewOfFile(view);
  ::CloseHandle(mapping);
  file.Close();
  pointed = PointShortcutsAt(chrome_exe_, model::AppIcon::kSunset);
  EXPECT_EQ(pointed.rewritten, 1);
  EXPECT_EQ(pointed.failed, 0);
  base::win::ShortcutProperties found;
  ASSERT_TRUE(base::win::ResolveShortcutProperties(
      start, base::win::ShortcutProperties::PROPERTIES_ICON, &found));
  EXPECT_EQ(found.icon, app_.Append(L"AppIcon-Sunset.ico"));
}

// The uninstall takes Pane's pins, a pin's file the taskbar doesn't list
// included, and leaves another app's.
TEST_F(WindowsIntegrationTest, UnregisterTakesPanesPinsAlone) {
  ASSERT_EQ(Register(chrome_exe_, data_, /*first_install=*/true),
            static_cast<DWORD>(ERROR_SUCCESS));
  base::FilePath pins;
  ASSERT_TRUE(base::PathService::Get(base::DIR_TASKBAR_PINS, &pins));
  base::win::ShortcutProperties pin;
  pin.set_target(chrome_exe_);
  ASSERT_TRUE(base::win::CreateOrUpdateShortcutLink(
      pins.Append(L"Pane.lnk"), pin,
      base::win::ShortcutOperation::kCreateAlways));
  const base::FilePath other_exe = temp_.GetPath().Append(L"other.exe");
  ASSERT_TRUE(base::WriteFile(other_exe, "MZ"));
  base::win::ShortcutProperties other;
  other.set_target(other_exe);
  ASSERT_TRUE(base::win::CreateOrUpdateShortcutLink(
      pins.Append(L"Other.lnk"), other,
      base::win::ShortcutOperation::kCreateAlways));
  ASSERT_TRUE(base::CreateDirectory(data_));

  ASSERT_EQ(Unregister(chrome_exe_, data_, /*erase=*/false, EraseProgress()),
            static_cast<DWORD>(ERROR_SUCCESS));
  EXPECT_FALSE(base::PathExists(pins.Append(L"Pane.lnk")));
  EXPECT_TRUE(base::PathExists(pins.Append(L"Other.lnk")));
}

}  // namespace pane::windows
