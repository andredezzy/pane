// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/windows_integration.h"

#include <windows.h>

#include <shlobj.h>
#include <windows.ui.notifications.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "base/win/core_winrt_util.h"
#include "base/win/registry.h"
#include "base/win/scoped_hstring.h"
#include "base/win/shortcut.h"
#include "chrome/install_static/install_util.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/installer_util_strings.h"
#include "chrome/installer/util/l10n_string_util.h"
#include "chrome/installer/util/registry_entry.h"
#include "chrome/installer/util/shell_util.h"
#include "pane/app/version.h"
#include "pane/model/app_settings.h"
#include "pane/model/data_lock.h"
#include "pane/model/panic_wipe.h"
#include "pane/model/persisted_file.h"
#include "pane/model/pin_guard.h"
#include "pane/model/preference_macs.h"

namespace pane::windows {

namespace {

// Pane's words (Paper 11.12.3, 11.12.5, approved with the boards): the
// README's line for the tooltip, and leading Default apps' description.
constexpr wchar_t kName[] = L"Pane";
constexpr wchar_t kTooltip[] = L"Every profile, its own internet.";
constexpr wchar_t kDescription[] =
    L"Every profile, its own internet: its own sign-ins, proxy, fingerprint, "
    L"and extensions.";
// pane/BRANDING's COMPANY_FULLNAME (ADR 0114).
constexpr wchar_t kPublisher[] = L"Rounded Corners";

class Words : public installer::TranslationDelegate {
 public:
  std::wstring GetLocalizedString(int id) override {
    switch (id) {
      case IDS_PRODUCT_NAME_BASE:
        return kName;
      case IDS_SHORTCUT_TOOLTIP_BASE:
        return kTooltip;
      case IDS_PRODUCT_DESCRIPTION_BASE:
        return kDescription;
      case IDS_ABOUT_VERSION_COMPANY_NAME_BASE:
        return kPublisher;
      case IDS_APP_SHORTCUTS_SUBDIR_NAME_BASE:
        return L"Pane Apps";
      case IDS_INBOUND_MDNS_RULE_NAME_BASE:
        return L"Pane (mDNS-In)";
      case IDS_INBOUND_MDNS_RULE_DESCRIPTION_BASE:
        return L"Inbound rule for Pane to allow mDNS traffic.";
    }
    // The rest are setup.exe's own, which Pane never runs.
    return std::wstring();
  }
};

// The icon files beside chrome.exe (W7a's): the app's, and the documents'.
constexpr wchar_t kAppIcon[] = L"Pane.ico";
constexpr wchar_t kPageIcon[] = L"PaneHTM.ico";
constexpr wchar_t kPdfIcon[] = L"PanePDF.ico";

// Windows' keys for Default apps (shell_util.cc's).
constexpr wchar_t kClasses[] = L"Software\\Classes\\";
constexpr wchar_t kStartMenuInternet[] =
    L"Software\\Clients\\StartMenuInternet\\";
constexpr wchar_t kRegisteredApplications[] =
    L"Software\\RegisteredApplications";

std::wstring StartMenuInternetKey() {
  return base::StrCat({kStartMenuInternet, install_static::GetBaseAppName()});
}

std::wstring CapabilitiesKey() {
  return StartMenuInternetKey() + L"\\Capabilities";
}

std::wstring IconOf(const base::FilePath& file) {
  return ShellUtil::FormatIconLocation(file, 0);
}

// The AppUserModelID the window carries (frame_win.cc), which the shortcuts
// and the ProgIDs name so the taskbar groups them as one.
std::wstring AppId() {
  return ShellUtil::GetBrowserModelId(InstallUtil::IsPerUserInstall());
}

// A ProgID's entries (shell_util.cc's GetProgIdEntries).
void AddProgId(std::vector<std::unique_ptr<RegistryEntry>>& entries,
               const std::wstring& prog_id,
               const std::wstring& description,
               const base::FilePath& icon,
               const base::FilePath& chrome_exe) {
  const base::FilePath app = chrome_exe.DirName();
  const std::wstring key = base::StrCat({kClasses, prog_id});
  entries.push_back(std::make_unique<RegistryEntry>(key, description));
  entries.push_back(
      std::make_unique<RegistryEntry>(key + L"\\DefaultIcon", IconOf(icon)));
  entries.push_back(std::make_unique<RegistryEntry>(
      key + L"\\shell\\open\\command",
      ShellUtil::GetChromeShellOpenCmd(chrome_exe)));
  entries.push_back(
      std::make_unique<RegistryEntry>(key, L"AppUserModelId", AppId()));
  const std::wstring application = key + L"\\Application";
  entries.push_back(
      std::make_unique<RegistryEntry>(application, L"AppUserModelId", AppId()));
  entries.push_back(std::make_unique<RegistryEntry>(
      application, L"ApplicationIcon", IconOf(app.Append(kAppIcon))));
  entries.push_back(
      std::make_unique<RegistryEntry>(application, L"ApplicationName", kName));
  entries.push_back(std::make_unique<RegistryEntry>(
      application, L"ApplicationDescription", kTooltip));
  entries.push_back(std::make_unique<RegistryEntry>(
      application, L"ApplicationCompany", kPublisher));
}

// The extensions and protocols Default apps lists (Paper 11.12.5): Chromium's
// files, the PDF, and the web's two protocols, never one Pane can't open.
std::vector<std::wstring_view> FileTypes() {
  std::vector<std::wstring_view> types(
      ShellUtil::kPotentialFileAssociations.begin(),
      ShellUtil::kPotentialFileAssociations.end());
  return types;
}

// Each value of HKCU's `key` but those named in `keep`, and each subkey.
bool EmptyKeyBut(const std::wstring& key, std::wstring_view keep) {
  base::win::RegKey root(HKEY_CURRENT_USER, key.c_str(), KEY_READ | KEY_WRITE);
  if (!root.Valid()) {
    return true;
  }
  bool removed = true;
  std::vector<std::wstring> subkeys;
  for (base::win::RegistryKeyIterator it(HKEY_CURRENT_USER, key.c_str());
       it.Valid(); ++it) {
    subkeys.emplace_back(it.Name());
  }
  for (const std::wstring& subkey : subkeys) {
    if (base::EqualsCaseInsensitiveASCII(subkey, keep)) {
      continue;
    }
    removed &= root.DeleteKey(subkey.c_str()) == ERROR_SUCCESS;
  }
  std::vector<std::wstring> values;
  for (base::win::RegistryValueIterator it(HKEY_CURRENT_USER, key.c_str());
       it.Valid(); ++it) {
    values.emplace_back(it.Name());
  }
  for (const std::wstring& value : values) {
    removed &= root.DeleteValue(value.c_str()) == ERROR_SUCCESS;
  }
  return removed;
}

DWORD DeleteTree(const std::wstring& key) {
  const LSTATUS status = ::RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
  if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) {
    // RegDeleteTree leaves the key itself.
    const LSTATUS deleted = ::RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
    return deleted == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS
                                           : static_cast<DWORD>(deleted);
  }
  return static_cast<DWORD>(status);
}

// The uninstall entry Installed apps lists (Paper 11.12.4).
DWORD WriteUninstallEntry(const base::FilePath& chrome_exe) {
  const base::FilePath app = chrome_exe.DirName();
  base::win::RegKey key;
  if (const LONG result = key.Create(
          HKEY_CURRENT_USER, install_static::GetUninstallRegistryPath().c_str(),
          KEY_SET_VALUE)) {
    return static_cast<DWORD>(result);
  }
  base::Time::Exploded today;
  base::Time::Now().LocalExplode(&today);
  int64_t size = 0;
  base::FileEnumerator files(app, /*recursive=*/true,
                             base::FileEnumerator::FILES);
  for (base::FilePath file = files.Next(); !file.empty(); file = files.Next()) {
    size += files.GetInfo().GetSize();
  }
  const std::wstring uninstall =
      base::StrCat({L"\"", chrome_exe.value(), L"\" --", L"pane-uninstall"});
  const std::array<std::pair<const wchar_t*, std::wstring>, 7> strings = {{
      {L"DisplayName", kName},
      {L"Publisher", kPublisher},
      {L"DisplayVersion", base::UTF8ToWide(PaneVersion())},
      {L"InstallDate",
       base::UTF8ToWide(base::StringPrintf("%04d%02d%02d", today.year,
                                           today.month, today.day_of_month))},
      {L"DisplayIcon", app.Append(kAppIcon).value()},
      {L"UninstallString", uninstall},
      {L"InstallLocation", app.value()},
  }};
  for (const auto& [name, value] : strings) {
    if (const LONG result = key.WriteValue(name, value.c_str())) {
      return static_cast<DWORD>(result);
    }
  }
  const std::array<std::pair<const wchar_t*, DWORD>, 3> numbers = {{
      {L"EstimatedSize", static_cast<DWORD>(size / 1024)},
      {L"NoModify", 1},
      {L"NoRepair", 1},
  }};
  for (const auto& [name, value] : numbers) {
    if (const LONG result = key.WriteValue(name, value)) {
      return static_cast<DWORD>(result);
    }
  }
  return ERROR_SUCCESS;
}

// Pane's toast activator, the COM server Windows starts for a toast's click
// (install_worker.cc's AddNativeNotificationWorkItems).
DWORD WriteToastActivator(const base::FilePath& chrome_exe) {
  const std::wstring key = InstallUtil::GetToastActivatorRegistryPath();
  DeleteTree(key);
  const base::FilePath helper =
      chrome_exe.DirName().Append(L"notification_helper.exe");
  base::win::RegKey server;
  if (const LONG result =
          server.Create(HKEY_CURRENT_USER, (key + L"\\LocalServer32").c_str(),
                        KEY_SET_VALUE)) {
    return static_cast<DWORD>(result);
  }
  if (const LONG result = server.WriteValue(
          L"", base::StrCat({L"\"", helper.value(), L"\""}).c_str())) {
    return static_cast<DWORD>(result);
  }
  return static_cast<DWORD>(
      server.WriteValue(L"ServerExecutable", helper.value().c_str()));
}

// Start's and the desktop's shortcut, as ShellUtil makes Chrome's, naming
// the App icon's sky (call D), the AppUserModelID, and the toast activator.
ShellUtil::ShortcutProperties ShortcutProperties(
    const base::FilePath& chrome_exe,
    model::AppIcon sky) {
  ShellUtil::ShortcutProperties properties(ShellUtil::CURRENT_USER);
  properties.set_icon(SkyIcon(chrome_exe.DirName(), sky), 0);
  properties.set_toast_activator_clsid(
      install_static::GetToastActivatorClsid());
  ShellUtil::AddDefaultShortcutProperties(chrome_exe, &properties);
  return properties;
}

// Every shortcut of Pane's, by path: Start's, the desktop's, and the
// taskbar's pins.
std::vector<base::FilePath> ShortcutsOf(const base::FilePath& chrome_exe) {
  std::vector<base::FilePath> shortcuts;
  const std::wstring name =
      base::StrCat({InstallUtil::GetShortcutName(), L".lnk"});
  for (const ShellUtil::ShortcutLocation location :
       {ShellUtil::SHORTCUT_LOCATION_START_MENU_ROOT,
        ShellUtil::SHORTCUT_LOCATION_DESKTOP}) {
    base::FilePath folder;
    if (ShellUtil::GetShortcutPath(location, ShellUtil::CURRENT_USER,
                                   &folder)) {
      shortcuts.push_back(folder.Append(name));
    }
  }
  base::FilePath pins;
  if (ShellUtil::GetShortcutPath(ShellUtil::SHORTCUT_LOCATION_TASKBAR_PINS,
                                 ShellUtil::CURRENT_USER, &pins)) {
    base::FileEnumerator links(pins, /*recursive=*/false,
                               base::FileEnumerator::FILES, L"*.lnk");
    for (base::FilePath link = links.Next(); !link.empty();
         link = links.Next()) {
      shortcuts.push_back(link);
    }
  }
  return shortcuts;
}

bool SamePath(const base::FilePath& a, const base::FilePath& b) {
  return base::FilePath::CompareEqualIgnoreCase(
      a.NormalizePathSeparators().value(), b.NormalizePathSeparators().value());
}

// The data's erase, the panic wipe's way (ADR 0042, 0113).
DWORD EraseData(const base::FilePath& data,
                std::unique_ptr<model::DataLock> lock,
                const EraseProgress& progress) {
  uint64_t total = 0;
  base::FileEnumerator files(data, /*recursive=*/true,
                             base::FileEnumerator::FILES);
  for (base::FilePath file = files.Next(); !file.empty(); file = files.Next()) {
    total += files.GetInfo().GetSize();
  }
  uint64_t erased = 0;
  const auto count = base::BindRepeating(
      [](uint64_t* erased, uint64_t total, const EraseProgress& progress,
         int64_t bytes) {
        *erased += static_cast<uint64_t>(std::max<int64_t>(bytes, 0));
        if (progress) {
          progress.Run(std::min(*erased, total), total);
        }
      },
      base::Unretained(&erased), total, progress);
  if (progress) {
    progress.Run(0, total);
  }
  std::vector<model::WipeFailure> failures;
  // The TPM's keys of the PIN, then both copies of the sealed data key: the
  // rest reads as nothing from here on.
  model::PinGuard::ForData(data)->Forget(std::nullopt);
  for (const char* name : {"Data.security.json", "security.json"}) {
    std::ranges::move(model::EraseForWipe(data.AppendASCII(name), 3, count),
                      std::back_inserter(failures));
  }
  // The engine's preference MACs, which Chromium keeps in the registry
  // (ADR 0121).
  if (!model::ForgetEveryPreferenceMacs(install_static::GetRegistryPath())) {
    LOG(ERROR) << "Could not forget the preference MACs";
  }
  // The rest staged and erased as the panic wipe stages and erases it, so an
  // erase cut short is finished by Pane's next launch or the next uninstall.
  std::ranges::move(model::StageWipe(data, std::nullopt),
                    std::back_inserter(failures));
  std::ranges::move(
      model::EraseStagedWipes(data, std::nullopt, std::nullopt, count),
      std::back_inserter(failures));
  // The data's lock lets go of its file, which goes with what is left.
  lock.reset();
  std::ranges::move(model::EraseForWipe(data, 1, count),
                    std::back_inserter(failures));
  for (const model::WipeFailure& failure : failures) {
    LOG(ERROR) << "Uninstall's erase: " << failure.ToString();
  }
  if (progress) {
    progress.Run(total, total);
  }
  return base::PathExists(data) ? static_cast<DWORD>(ERROR_DIR_NOT_EMPTY)
                                : static_cast<DWORD>(ERROR_SUCCESS);
}

}  // namespace

installer::TranslationDelegate& PanesWords() {
  static base::NoDestructor<Words> words;
  return *words;
}

void UsePanesWords() {
  installer::SetTranslationDelegate(&PanesWords());
}

base::FilePath SkyIcon(const base::FilePath& app_folder, model::AppIcon sky) {
  std::string name(model::AppIconName(sky));
  if (!name.empty()) {
    name[0] = base::ToUpperASCII(name[0]);
  }
  return app_folder.Append(
      base::UTF8ToWide(base::StrCat({"AppIcon-", name, ".ico"})));
}

model::AppIcon ShortcutSky(const base::FilePath& data_folder) {
  // settings.json as the model reads it, in its envelope ({"state":
  // {"settings": …}, "version": 0}); left as it is when it won't read, which
  // Pane's next launch keeps aside itself.
  model::AppSettings settings;
  const base::expected<std::optional<base::Value>, model::FileError> state =
      model::PersistedFile(data_folder, "settings").Load();
  if (state.has_value() && *state) {
    if (std::optional<model::AppSettings> found =
            model::SettingsFromState(**state)) {
      settings = std::move(*found);
    }
  }
  bool dark = settings.theme == model::Theme::kDark;
  if (settings.theme == model::Theme::kSystem) {
    DWORD light = 1;
    base::win::RegKey personalize(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        KEY_QUERY_VALUE);
    dark = personalize.ReadValueDW(L"AppsUseLightTheme", &light) ==
               ERROR_SUCCESS &&
           light == 0;
  }
  return model::Resolved(settings.app_icon, dark);
}

std::vector<std::unique_ptr<RegistryEntry>> DefaultAppsEntries(
    const base::FilePath& chrome_exe) {
  const base::FilePath app = chrome_exe.DirName();
  const std::wstring page_prog_id = install_static::GetBrowserProgIdPrefix();
  const std::wstring pdf_prog_id = install_static::GetPDFProgIdPrefix();
  std::vector<std::unique_ptr<RegistryEntry>> entries;
  AddProgId(entries, page_prog_id,
            install_static::GetBrowserProgIdDescription(),
            app.Append(kPageIcon), chrome_exe);
  AddProgId(entries, pdf_prog_id, install_static::GetPDFProgIdDescription(),
            app.Append(kPdfIcon), chrome_exe);

  // StartMenuInternet: Pane among the browsers.
  const std::wstring client = StartMenuInternetKey();
  const std::wstring quoted = base::StrCat({L"\"", chrome_exe.value(), L"\""});
  entries.push_back(std::make_unique<RegistryEntry>(client, kName));
  entries.push_back(std::make_unique<RegistryEntry>(
      client + L"\\shell\\open\\command", quoted));
  entries.push_back(std::make_unique<RegistryEntry>(
      client + L"\\DefaultIcon", IconOf(app.Append(kAppIcon))));

  // Capabilities, and RegisteredApplications naming them: Default apps'
  // entry.
  const std::wstring capabilities = CapabilitiesKey();
  entries.push_back(std::make_unique<RegistryEntry>(
      kRegisteredApplications, install_static::GetBaseAppName(), capabilities));
  entries.push_back(std::make_unique<RegistryEntry>(
      capabilities, L"ApplicationDescription", kDescription));
  entries.push_back(std::make_unique<RegistryEntry>(
      capabilities, L"ApplicationIcon", IconOf(app.Append(kAppIcon))));
  entries.push_back(
      std::make_unique<RegistryEntry>(capabilities, L"ApplicationName", kName));
  entries.push_back(std::make_unique<RegistryEntry>(
      capabilities + L"\\Startmenu", L"StartMenuInternet",
      install_static::GetBaseAppName()));
  for (const std::wstring_view type : FileTypes()) {
    entries.push_back(
        std::make_unique<RegistryEntry>(capabilities + L"\\FileAssociations",
                                        std::wstring(type), page_prog_id));
    entries.push_back(std::make_unique<RegistryEntry>(
        base::StrCat({kClasses, type, L"\\OpenWithProgids"}), page_prog_id,
        std::wstring()));
  }
  entries.push_back(std::make_unique<RegistryEntry>(
      capabilities + L"\\FileAssociations", L".pdf", pdf_prog_id));
  entries.push_back(std::make_unique<RegistryEntry>(
      base::StrCat({kClasses, L".pdf\\OpenWithProgids"}), pdf_prog_id,
      std::wstring()));
  for (const std::wstring_view protocol :
       ShellUtil::kBrowserProtocolAssociations) {
    entries.push_back(
        std::make_unique<RegistryEntry>(capabilities + L"\\URLAssociations",
                                        std::wstring(protocol), page_prog_id));
  }
  return entries;
}

DWORD Register(const base::FilePath& chrome_exe,
               const base::FilePath& data_folder,
               bool first_install) {
  if (const DWORD error = WriteToastActivator(chrome_exe)) {
    LOG(ERROR) << "Could not register the toast activator: " << error;
    return error;
  }
  if (!ShellUtil::AddRegistryEntries(HKEY_CURRENT_USER,
                                     DefaultAppsEntries(chrome_exe))) {
    LOG(ERROR) << "Could not register Pane in Default apps";
    return ERROR_REGISTRY_IO_FAILED;
  }
  ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  if (const DWORD error = WriteUninstallEntry(chrome_exe)) {
    LOG(ERROR) << "Could not write the uninstall entry: " << error;
    return error;
  }
  // Start's always, a reinstall's repair too; the desktop's at a first
  // install alone, so one the user deleted never comes back (11.12.3).
  if (!MakeStartShortcut(chrome_exe, data_folder)) {
    LOG(ERROR) << "Could not make Start's shortcut";
    return ERROR_CANNOT_MAKE;
  }
  ShellUtil::CreateOrUpdateShortcut(
      ShellUtil::SHORTCUT_LOCATION_DESKTOP,
      ShortcutProperties(chrome_exe, ShortcutSky(data_folder)),
      first_install ? ShellUtil::SHELL_SHORTCUT_CREATE_ALWAYS
                    : ShellUtil::SHELL_SHORTCUT_REPLACE_EXISTING);
  return ERROR_SUCCESS;
}

bool MakeStartShortcut(const base::FilePath& chrome_exe,
                       const base::FilePath& data_folder) {
  return ShellUtil::CreateOrUpdateShortcut(
      ShellUtil::SHORTCUT_LOCATION_START_MENU_ROOT,
      ShortcutProperties(chrome_exe, ShortcutSky(data_folder)),
      ShellUtil::SHELL_SHORTCUT_CREATE_ALWAYS);
}

DWORD Unregister(const base::FilePath& chrome_exe,
                 const base::FilePath& data_folder,
                 bool erase,
                 const EraseProgress& progress) {
  // A Pane on the data holds its lock: nothing changes while it runs.
  std::unique_ptr<model::DataLock> lock;
  if (base::DirectoryExists(data_folder)) {
    lock = model::DataLock::Take(data_folder);
    if (!lock) {
      return kDataInUse;
    }
  }
  DWORD failed = ERROR_SUCCESS;
  if (erase && lock) {
    failed = EraseData(data_folder, std::move(lock), progress);
  }
  lock.reset();

  // Both shortcuts, and any pin naming chrome.exe, as Chrome's uninstall
  // removes its own: the taskbar unpins what it lists (IPinnedList3). A pin's
  // file it doesn't list, which it left behind, goes too: what it names is
  // going.
  ShellUtil::RemoveAllShortcuts(ShellUtil::CURRENT_USER, {chrome_exe});
  for (const base::FilePath& shortcut : ShortcutsOf(chrome_exe)) {
    base::win::ShortcutProperties found;
    if (base::PathExists(shortcut) &&
        base::win::ResolveShortcutProperties(
            shortcut, base::win::ShortcutProperties::PROPERTIES_TARGET,
            &found) &&
        SamePath(found.target, chrome_exe)) {
      base::DeleteFile(shortcut);
    }
  }

  // Default apps: the ProgIDs, the client, its capabilities, and what names
  // them; Windows gives each type back to its earlier default (11.12.5).
  for (const std::wstring& key :
       {base::StrCat({kClasses, install_static::GetBrowserProgIdPrefix()}),
        base::StrCat({kClasses, install_static::GetPDFProgIdPrefix()}),
        StartMenuInternetKey()}) {
    if (const DWORD error = DeleteTree(key)) {
      failed = failed ? failed : error;
    }
  }
  base::win::RegKey registered(HKEY_CURRENT_USER, kRegisteredApplications,
                               KEY_SET_VALUE);
  if (registered.Valid()) {
    registered.DeleteValue(install_static::GetBaseAppName().c_str());
  }
  std::vector<std::wstring_view> types = FileTypes();
  types.push_back(L".pdf");
  for (const std::wstring_view type : types) {
    base::win::RegKey open_with(
        HKEY_CURRENT_USER,
        base::StrCat({kClasses, type, L"\\OpenWithProgids"}).c_str(),
        KEY_SET_VALUE);
    if (open_with.Valid()) {
      open_with.DeleteValue(install_static::GetBrowserProgIdPrefix());
      open_with.DeleteValue(install_static::GetPDFProgIdPrefix());
    }
  }
  ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

  // The toast activator.
  if (const DWORD error =
          DeleteTree(InstallUtil::GetToastActivatorRegistryPath())) {
    failed = failed ? failed : error;
  }

  // Chromium's own key: all of it with the data, or where there is no data
  // to keep, or all but the data's preference MACs (an uninstall with no data
  // kept them for nothing, W8d).
  const std::wstring own = install_static::GetRegistryPath();
  if (erase || !base::DirectoryExists(data_folder)) {
    if (const DWORD error = DeleteTree(own)) {
      failed = failed ? failed : error;
    }
  } else if (!EmptyKeyBut(own, L"PreferenceMACs")) {
    failed = failed ? failed : static_cast<DWORD>(ERROR_REGISTRY_IO_FAILED);
  }

  // Installed apps' entry last, and only once everything else has gone: an
  // uninstall that failed stays there, to be run again, its app folder
  // untouched (pane/installer/removal.h).
  if (!failed) {
    failed = DeleteTree(install_static::GetUninstallRegistryPath());
  }
  return failed;
}

bool ClearToasts() {
  namespace notifications = ABI::Windows::UI::Notifications;
  Microsoft::WRL::ComPtr<notifications::IToastNotificationManagerStatics>
      manager;
  Microsoft::WRL::ComPtr<notifications::IToastNotificationManagerStatics2>
      manager2;
  Microsoft::WRL::ComPtr<notifications::IToastNotificationHistory> history;
  HRESULT result = base::win::GetActivationFactory<
      notifications::IToastNotificationManagerStatics,
      RuntimeClass_Windows_UI_Notifications_ToastNotificationManager>(&manager);
  if (SUCCEEDED(result)) {
    result = manager.As(&manager2);
  }
  if (SUCCEEDED(result)) {
    result = manager2->get_History(&history);
  }
  if (SUCCEEDED(result)) {
    result =
        history->ClearWithId(base::win::ScopedHString::Create(AppId()).get());
  }
  if (FAILED(result)) {
    LOG(ERROR) << "Windows' notification center kept Pane's toasts: 0x"
               << std::hex << result;
    return false;
  }
  return true;
}

PointedShortcuts PointShortcutsAt(const base::FilePath& chrome_exe,
                                  model::AppIcon sky) {
  PointedShortcuts pointed;
  const base::FilePath icon = SkyIcon(chrome_exe.DirName(), sky);
  if (!base::PathExists(icon)) {
    return pointed;
  }
  for (const base::FilePath& shortcut : ShortcutsOf(chrome_exe)) {
    if (!base::PathExists(shortcut)) {
      continue;
    }
    base::win::ShortcutProperties found;
    if (!base::win::ResolveShortcutProperties(
            shortcut,
            base::win::ShortcutProperties::PROPERTIES_TARGET |
                base::win::ShortcutProperties::PROPERTIES_ICON,
            &found)) {
      ++pointed.failed;
      continue;
    }
    if (!SamePath(found.target, chrome_exe) ||
        (SamePath(found.icon, icon) && found.icon_index == 0)) {
      continue;
    }
    base::win::ShortcutProperties update;
    update.set_icon(icon, 0);
    if (!base::win::CreateOrUpdateShortcutLink(
            shortcut, update, base::win::ShortcutOperation::kUpdateExisting)) {
      ++pointed.failed;
      continue;
    }
    ::SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSHNOWAIT,
                     shortcut.value().c_str(), nullptr);
    ++pointed.rewritten;
  }
  return pointed;
}

}  // namespace pane::windows
