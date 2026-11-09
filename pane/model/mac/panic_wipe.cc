// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/panic_wipe.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "crypto/random.h"
#include "pane/model/mac/electron_migration.h"
#include "pane/model/mac/file_operations.h"
#include "pane/model/persisted_file.h"

namespace pane::model {

namespace {

// What an earlier engine kept in the clear at its user data dir's root
// (StageEngineTraces).
constexpr std::array<std::string_view, 4> kEngineTraces = {
    "segmentation_platform", "BrowserMetrics", "BrowserMetrics-spare.pma",
    "DeferredBrowserMetrics"};

// As UUID().uuidString names it, for a staging folder.
std::string NewStageId() {
  return base::ToUpperASCII(base::Uuid::GenerateRandomV4().AsLowercaseString());
}

WipeFailure Failure(const base::FilePath& path, std::string reason) {
  return {.path = path.value(), .reason = std::move(reason)};
}

// errno's text for the call that just failed.
std::string LastError() {
  return logging::SystemErrorCodeToString(logging::GetLastSystemErrorCode());
}

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

// Where staged data waits, each root with its passes: the data directory, the
// caches, and beside the Electron app's folder and its caches.
std::vector<std::pair<std::optional<base::FilePath>, int>> Roots(
    const base::FilePath& data_directory,
    const std::optional<base::FilePath>& cache_directory,
    const std::optional<ElectronData>& electron) {
  std::optional<base::FilePath> electron_root;
  std::optional<base::FilePath> electron_caches_root;
  if (electron) {
    electron_root = electron->directory.DirName();
    if (electron->cache_directory) {
      electron_caches_root = electron->cache_directory->DirName();
    }
  }
  return {{data_directory, 3},
          {cache_directory, 1},
          {electron_root, 3},
          {electron_caches_root, 1}};
}

// Passes of random bytes over the whole file, each synced to disk.
std::vector<WipeFailure> Overwrite(const base::FilePath& path, int passes) {
  base::File::Info info;
  if (!base::GetFileInfo(path, &info)) {
    return {Failure(path, LastError())};
  }
  if (info.size <= 0) {
    return {};
  }

  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  if (!file.IsValid()) {
    return {Failure(path, base::File::ErrorToString(file.error_details()))};
  }

  const int64_t size = info.size;
  std::vector<uint8_t> bytes(
      static_cast<size_t>(std::min<int64_t>(1 << 20, size)));
  for (int pass = 0; pass < passes; ++pass) {
    int64_t offset = 0;
    while (offset < size) {
      const base::span<uint8_t> chunk = base::span(bytes).first(
          static_cast<size_t>(std::min<int64_t>(1 << 20, size - offset)));
      crypto::RandBytes(chunk);
      if (!file.WriteAndCheck(offset, chunk)) {
        return {Failure(path, LastError())};
      }
      offset += static_cast<int64_t>(chunk.size());
    }
    // As FileHandle.synchronize, fsync(2), where base::File::Flush asks only
    // for a barrier.
    if (HANDLE_EINTR(fsync(file.GetPlatformFile())) != 0) {
      return {Failure(path, LastError())};
    }
  }
  return {};
}

}  // namespace

std::string WipeFailure::ToString() const {
  return base::StrCat({path, ": ", reason});
}

std::vector<base::FilePath> WipeTargets(
    const base::FilePath& data_directory,
    const std::optional<base::FilePath>& cache_directory,
    const std::optional<ElectronData>& electron) {
  std::vector<base::FilePath> targets;
  for (std::string_view name : {"Engine", "Data.sparsebundle", "profiles.json",
                                "settings.json", "deletions.json"}) {
    targets.push_back(data_directory.Append(name));
  }
  if (cache_directory) {
    targets.push_back(cache_directory->Append("Engine"));
  }
  if (electron) {
    targets.push_back(electron->directory);
    if (electron->cache_directory) {
      targets.push_back(*electron->cache_directory);
    }
  }
  for (std::string_view name : {"Data.security.json", "security.json"}) {
    targets.push_back(data_directory.Append(name));
  }
  return targets;
}

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

std::vector<WipeFailure> StageWipe(
    const base::FilePath& data_directory,
    const std::optional<base::FilePath>& cache_directory,
    const std::optional<ElectronData>& electron) {
  const std::string id = NewStageId();
  std::vector<WipeFailure> failures;

  for (const base::FilePath& target :
       WipeTargets(data_directory, cache_directory, electron)) {
    if (!base::PathExists(target) && !base::IsLink(target)) {
      continue;
    }
    if (electron && target == electron->directory &&
        !base::PathExists(target.Append("profiles.json"))) {
      continue;
    }

    const base::FilePath folder =
        target.DirName().Append(kWipeStagingName).Append(id);
    if (!base::CreateDirectory(folder)) {
      failures.push_back(Failure(target, LastFileError(folder)));
      continue;
    }
    if (base::expected<void, std::string> moved =
            MoveItem(target, folder.Append(target.BaseName()));
        !moved.has_value()) {
      failures.push_back(Failure(target, moved.error()));
    }
  }

  return failures;
}

std::vector<WipeFailure> StageEngineTraces(
    const base::FilePath& data_directory) {
  std::vector<base::FilePath> traces;
  for (std::string_view trace : kEngineTraces) {
    const base::FilePath path = data_directory.Append("Engine").Append(trace);
    if (base::PathExists(path)) {
      traces.push_back(path);
    }
  }
  if (traces.empty()) {
    return {};
  }

  const base::FilePath folder =
      data_directory.Append(kWipeStagingName).Append(NewStageId());
  std::vector<WipeFailure> failures;
  if (!base::CreateDirectory(folder)) {
    const std::string reason = LastFileError(folder);
    for (const base::FilePath& trace : traces) {
      failures.push_back(Failure(trace, reason));
    }
    return failures;
  }

  for (const base::FilePath& trace : traces) {
    if (base::expected<void, std::string> moved =
            MoveItem(trace, folder.Append(trace.BaseName()));
        !moved.has_value()) {
      failures.push_back(Failure(trace, moved.error()));
    }
  }
  return failures;
}

std::vector<StagedWipe> StagedWipes(
    const base::FilePath& data_directory,
    const std::optional<base::FilePath>& cache_directory,
    const std::optional<ElectronData>& electron) {
  std::vector<StagedWipe> staged;
  for (const auto& [root, passes] :
       Roots(data_directory, cache_directory, electron)) {
    if (!root) {
      continue;
    }
    base::FileEnumerator wipes(root->Append(kWipeStagingName),
                               /*recursive=*/false,
                               base::FileEnumerator::FILES |
                                   base::FileEnumerator::DIRECTORIES |
                                   base::FileEnumerator::SHOW_SYM_LINKS);
    for (base::FilePath wipe = wipes.Next(); !wipe.empty();
         wipe = wipes.Next()) {
      staged.push_back({.path = wipe, .passes = passes});
    }
  }
  return staged;
}

std::vector<WipeFailure> EraseStagedWipes(
    const base::FilePath& data_directory,
    const std::optional<base::FilePath>& cache_directory,
    const std::optional<ElectronData>& electron) {
  std::vector<WipeFailure> failures;
  for (const StagedWipe& staged :
       StagedWipes(data_directory, cache_directory, electron)) {
    std::ranges::move(EraseForWipe(staged.path, staged.passes),
                      std::back_inserter(failures));
  }

  for (const auto& root : Roots(data_directory, cache_directory, electron)) {
    if (root.first) {
      // Only once empty: a wipe staged meanwhile keeps its own.
      rmdir(root.first->Append(kWipeStagingName).value().c_str());
    }
  }

  return failures;
}

std::vector<WipeFailure> EraseForWipe(const base::FilePath& path, int passes) {
  if (path.FinalExtension() == ".sparsebundle") {
    passes = 0;
  }

  base::stat_wrapper_t info;
  if (base::File::Lstat(path, &info) != 0) {
    // Gone already: nothing to erase.
    if (errno == ENOENT) {
      return {};
    }
    return {Failure(path, LastError())};
  }

  std::vector<WipeFailure> failures;
  if (S_ISDIR(info.st_mode)) {
    base::FileEnumerator entries(
        path, /*recursive=*/false,
        base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
            base::FileEnumerator::SHOW_SYM_LINKS,
        base::FilePath::StringType(),
        base::FileEnumerator::FolderSearchPolicy::MATCH_ONLY,
        base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
    for (base::FilePath entry = entries.Next(); !entry.empty();
         entry = entries.Next()) {
      std::ranges::move(EraseForWipe(entry, passes),
                        std::back_inserter(failures));
    }
    if (entries.GetError() != base::File::FILE_OK) {
      failures.push_back(
          Failure(path, base::File::ErrorToString(entries.GetError())));
    }
  } else if (S_ISREG(info.st_mode)) {
    std::ranges::move(Overwrite(path, passes), std::back_inserter(failures));
  }

  if (base::expected<void, std::string> removed = RemoveItem(path);
      !removed.has_value()) {
    failures.push_back(Failure(path, removed.error()));
  }

  return failures;
}

}  // namespace pane::model
