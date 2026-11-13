// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/wipe_sources.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/base_paths.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "pane/model/mac/electron_migration.h"
#include "pane/model/mac/file_operations.h"
#include "pane/model/mac/proxy_password_store.h"
#include "pane/model/mac/volume_place.h"
#include "pane/model/persisted_file.h"

namespace pane::model {

namespace {

// As URL.standardizedFileURL.pathComponents gives them: "/" first.
std::vector<std::string> PathComponents(const base::FilePath& path) {
  return StandardizedPath(path).GetComponents();
}

bool StartsWith(const std::vector<std::string>& components,
                const std::vector<std::string>& prefix) {
  return components.size() >= prefix.size() &&
         std::ranges::equal(base::span(components).first(prefix.size()),
                            prefix);
}

}  // namespace

std::optional<ElectronData> ElectronDataToWipe(
    const base::FilePath& data_directory,
    const base::FilePath& application_support,
    const base::FilePath& caches) {
  const base::expected<std::optional<base::Value>, FileError> state =
      PersistedFile(data_directory, kMigrationRecordName).Load();
  const std::optional<MigrationRecord> record =
      state.has_value() && *state ? MigrationRecord::FromValue(**state)
                                  : std::nullopt;
  if (!record || !record->source.starts_with("/")) {
    return std::nullopt;
  }

  const base::FilePath source =
      StandardizedPath(base::FilePath(record->source));
  const std::vector<std::string> data = PathComponents(data_directory);
  const std::vector<std::string> components = PathComponents(source);

  // "/", a volume, a home folder: never a source to erase.
  if (components.size() <= 4 || StartsWith(data, components) ||
      StartsWith(components, data)) {
    return std::nullopt;
  }

  return ElectronData{.directory = source,
                      .cache_directory = CacheDirectoryFor(
                          source, application_support, caches)};
}

std::optional<base::FilePath> CacheDirectoryFor(
    const base::FilePath& data_directory,
    const base::FilePath& application_support,
    const base::FilePath& caches) {
  const std::vector<std::string> data = PathComponents(data_directory);
  const std::vector<std::string> support = PathComponents(application_support);
  if (data.size() <= support.size() || !StartsWith(data, support)) {
    return std::nullopt;
  }
  base::FilePath directory = caches;
  for (const std::string& component :
       base::span(data).subspan(support.size())) {
    directory = directory.Append(component);
  }
  return directory;
}

std::optional<base::FilePath> ProfileCacheDirectory(
    const base::FilePath& data_directory) {
  base::FilePath application_support;
  base::FilePath caches;
  if (!base::PathService::Get(base::DIR_APP_DATA, &application_support) ||
      !base::PathService::Get(base::DIR_CACHE, &caches)) {
    return std::nullopt;
  }
  return CacheDirectoryFor(data_directory, application_support, caches);
}

std::optional<ElectronData> ElectronDataFor(
    const base::FilePath& data_directory) {
  base::FilePath application_support;
  base::FilePath caches;
  if (!base::PathService::Get(base::DIR_APP_DATA, &application_support) ||
      !base::PathService::Get(base::DIR_CACHE, &caches)) {
    return std::nullopt;
  }
  return ElectronDataToWipe(data_directory, application_support, caches);
}

void ForgetElectronKey(const std::optional<ElectronData>& electron,
                       SafeStorage safe_storage) {
  if (!electron) {
    return;
  }
  const std::string& path = electron->directory.value();
  const auto name =
      std::ranges::find_if(kElectronAppNames, [&](std::string_view name) {
        return path.ends_with(base::StrCat({"/", name}));
      });
  if (name == kElectronAppNames.end()) {
    return;
  }
  const OSStatus status =
      safe_storage.DeleteKey(base::StrCat({*name, " Safe Storage"}), *name);
  VLOG(1) << "The panic wipe removed the Electron app's cookie key: " << status;
}

WipeExtras MacWipeExtras(const base::FilePath& data_directory,
                         scoped_refptr<VolumeTools> tools,
                         SafeStorage safe_storage) {
  WipeExtras extras;
  extras.cache_directory = ProfileCacheDirectory(data_directory);
  extras.electron = ElectronDataFor(data_directory);
  extras.at_once = base::BindRepeating(
      [](const base::FilePath& data_directory, scoped_refptr<VolumeTools> tools,
         SafeStorage safe_storage) {
        ProxyPasswordStore(data_directory, safe_storage).Delete();
        // The wiped instance's volume, which its watchdog went down with: its
        // key is gone, and it moves aside detached.
        tools->DetachLeftovers();
      },
      data_directory, std::move(tools), safe_storage);
  extras.off_the_launch =
      base::BindRepeating(&ForgetElectronKey, extras.electron, safe_storage);
  return extras;
}

}  // namespace pane::model
