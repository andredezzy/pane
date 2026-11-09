// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/model_store.h"

#include <utility>
#include <vector>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "pane/model/persisted_file.h"
#include "pane/model/profile.h"
#include "pane/model/proxy_passwords.h"
#include "pane/model/sealed_file.h"

namespace pane::model {

namespace {

constexpr char kProfilesName[] = "profiles";
constexpr char kSettingsName[] = "settings";

// What a save makes of the model's profiles, on the store's sequence: their
// state sealed under the files key. A text that can't be made writes nothing,
// and says so (`failed`): the writer tells nothing of it.
std::optional<std::string> SealProfiles(
    const base::FilePath& directory,
    const std::vector<BrowserProfile>& profiles,
    const Key& key,
    base::OnceClosure failed) {
  // The data directory is there from the launch on; a test's may not be.
  std::optional<std::string> text;
  if (base::CreateDirectory(directory)) {
    text =
        SealedFile(directory, kProfilesName).Seal(ProfilesState(profiles), key);
  }
  if (!text) {
    std::move(failed).Run();
  }
  return text;
}

std::optional<std::string> EncodeSettings(const base::FilePath& directory,
                                          const AppSettings& settings,
                                          base::OnceClosure failed) {
  std::optional<std::string> text;
  if (base::CreateDirectory(directory)) {
    text = PersistedFile::Encode(SettingsState(settings));
  }
  if (!text) {
    std::move(failed).Run();
  }
  return text;
}

// Once profiles.json is on the disk: the passwords it now holds leave the
// store they were kept in, then the store hears how the write went.
void AfterProfilesWrite(base::RepeatingClosure remove_kept_apart,
                        base::OnceCallback<void(bool)> report,
                        bool written) {
  if (written && remove_kept_apart) {
    remove_kept_apart.Run();
  }
  std::move(report).Run(written);
}

}  // namespace

// What a load read, on the store's sequence.
struct ModelStore::Loaded {
  std::vector<BrowserProfile> profiles;
  // They came from an earlier Pane's plain file: sealed at once.
  bool changed = false;
  // With the passwords it kept apart.
  bool kept_apart = false;
  // profiles.json is there, and the disk would not give it.
  bool unreadable = false;
};

ModelStore::KeptApart::KeptApart() = default;
ModelStore::KeptApart::KeptApart(const KeptApart&) = default;
ModelStore::KeptApart& ModelStore::KeptApart::operator=(const KeptApart&) =
    default;
ModelStore::KeptApart::~KeptApart() = default;

ModelStore::PendingWrite::PendingWrite() = default;
ModelStore::PendingWrite::~PendingWrite() = default;

void ModelStore::PendingWrite::Set(
    base::ImportantFileWriter::BackgroundDataProducerCallback make) {
  make_ = std::move(make);
}

base::ImportantFileWriter::BackgroundDataProducerCallback
ModelStore::PendingWrite::GetSerializedDataProducerForBackgroundSequence() {
  return std::move(make_);
}

// static
AppSettings ModelStore::LoadSettings(const base::FilePath& data_directory) {
  const PersistedFile file(data_directory, kSettingsName);
  base::expected<std::optional<base::Value>, FileError> state = file.Load();
  if (state.has_value()) {
    if (!*state) {
      return AppSettings();
    }
    if (std::optional<AppSettings> settings = SettingsFromState(**state)) {
      return std::move(*settings);
    }
  }
  // Never the path: it names the user's folders.
  LOG(ERROR) << "Could not read settings.json";
  KeepDamaged(file.path());
  return AppSettings();
}

ModelStore::ModelStore(const base::FilePath& data_directory,
                       AppSettings settings,
                       const base::Clock* clock,
                       FilesKey files_key,
                       KeptApart kept_apart)
    : data_directory_(data_directory),
      files_key_(std::move(files_key)),
      kept_apart_(std::move(kept_apart)),
      // An unlock waits for the profiles' load on it.
      files_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      profiles_writer_(data_directory.AppendASCII("profiles.json"),
                       files_,
                       base::TimeDelta()),
      settings_writer_(data_directory.AppendASCII("settings.json"),
                       files_,
                       base::TimeDelta()),
      model_(
          {},
          std::move(settings),
          clock,
          base::BindRepeating(&ModelStore::Persist, base::Unretained(this))) {}

ModelStore::~ModelStore() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void ModelStore::LoadProfiles(base::OnceClosure loaded) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::optional<Key> key = files_key_.Run();
  if (!key) {
    std::move(loaded).Run();
    return;
  }
  files_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ReadProfiles, data_directory_, *key, kept_apart_.read),
      base::BindOnce(&ModelStore::DidLoad, weak_factory_.GetWeakPtr(), loads_,
                     std::move(loaded)));
}

void ModelStore::DidLoad(int loads, base::OnceClosure loaded, Loaded result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (loads != loads_) {
    return;
  }
  if (result.unreadable) {
    LOG(ERROR) << "Could not read profiles.json: nothing is saved over it";
    std::move(loaded).Run();
    return;
  }
  profiles_loaded_ = true;
  passwords_kept_apart_ = result.kept_apart;
  model_.Load(std::move(result.profiles), result.changed);
  // An earlier Pane's file is sealed now, with the passwords it kept apart.
  if (result.changed) {
    model_.SaveNow();
  }
  std::move(loaded).Run();
}

void ModelStore::UnloadProfiles() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++loads_;
  model_.SaveNow();
  profiles_loaded_ = false;
  passwords_kept_apart_ = false;
  model_.Load({}, /*changed=*/false);
}

void ModelStore::SaveNow(base::OnceClosure written) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  model_.SaveNow();
  // Behind the writes the save just handed the writers.
  files_->PostTaskAndReply(FROM_HERE, base::DoNothing(), std::move(written));
}

void ModelStore::Persist(BrowserModel::Save save) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Before the key opens, and before the profiles have loaded, there is
  // nothing to save over profiles.json.
  const std::optional<Key> key = files_key_.Run();
  if (save.profiles && key && profiles_loaded_) {
    const bool kept_apart = passwords_kept_apart_;
    Write(profiles_writer_, pending_profiles_,
          base::BindOnce(&SealProfiles, data_directory_,
                         std::move(*save.profiles), *key,
                         base::BindPostTaskToCurrentDefault(base::BindOnce(
                             &ModelStore::Written, weak_factory_.GetWeakPtr(),
                             BrowserModel::File::kProfiles, false, false))),
          base::BindOnce(
              &AfterProfilesWrite,
              kept_apart ? kept_apart_.remove : base::RepeatingClosure(),
              base::BindPostTaskToCurrentDefault(base::BindOnce(
                  &ModelStore::Written, weak_factory_.GetWeakPtr(),
                  BrowserModel::File::kProfiles, kept_apart))));
  }
  if (save.settings) {
    Write(settings_writer_, pending_settings_,
          base::BindOnce(&EncodeSettings, data_directory_,
                         std::move(*save.settings),
                         base::BindPostTaskToCurrentDefault(base::BindOnce(
                             &ModelStore::Written, weak_factory_.GetWeakPtr(),
                             BrowserModel::File::kSettings, false, false))),
          base::BindPostTaskToCurrentDefault(
              base::BindOnce(&ModelStore::Written, weak_factory_.GetWeakPtr(),
                             BrowserModel::File::kSettings, false)));
  }
}

void ModelStore::Write(
    base::ImportantFileWriter& writer,
    PendingWrite& pending,
    base::ImportantFileWriter::BackgroundDataProducerCallback make,
    base::OnceCallback<void(bool)> after) {
  pending.Set(std::move(make));
  writer.RegisterOnNextWriteCallbacks(base::OnceClosure(), std::move(after));
  writer.ScheduleWriteWithBackgroundDataSerializer(&pending);
  // Posted now, behind every write before it: the model waited already.
  writer.DoScheduledWrite();
}

// A write that failed asks the model to save again.
void ModelStore::Written(BrowserModel::File file,
                         bool kept_apart,
                         bool written) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!written) {
    LOG(ERROR) << "Could not save "
               << (file == BrowserModel::File::kProfiles ? "profiles.json"
                                                         : "settings.json");
    model_.ScheduleSave({file});
    return;
  }
  if (kept_apart) {
    passwords_kept_apart_ = false;
  }
}

// static
ModelStore::Loaded ModelStore::ReadProfiles(
    const base::FilePath& directory,
    const Key& key,
    base::RepeatingCallback<std::optional<Passwords>()> read_kept_apart) {
  const SealedFile sealed(directory, kProfilesName);
  base::expected<std::optional<base::Value>, FileError> state =
      sealed.Load(key);
  // The disk would not give it: nothing says it is damaged, and nothing is
  // saved over it while it can't be read.
  if (!state.has_value() && state.error() == FileError::kUnreadable) {
    return {.unreadable = true};
  }
  if (state.has_value()) {
    if (!*state) {
      return {};
    }
    if (std::optional<std::vector<BrowserProfile>> profiles =
            ProfilesFromState(**state)) {
      return {.profiles = std::move(*profiles)};
    }
  }

  std::optional<std::vector<BrowserProfile>> profiles;
  base::expected<std::optional<base::Value>, FileError> plain =
      PersistedFile(directory, kProfilesName).Load();
  if (plain.has_value() && *plain) {
    profiles = ProfilesFromState(**plain);
  }
  if (!profiles) {
    // Never the path: it names the user's folders.
    LOG(ERROR) << "Could not open profiles.json";
    KeepDamaged(sealed.path());
    return {};
  }

  Loaded loaded{.changed = true};
  // The store is read only for a proxy that signs in: macOS may ask first.
  if (read_kept_apart && NeedsProxyPasswordStore(*profiles)) {
    if (std::optional<Passwords> stored = read_kept_apart.Run()) {
      *profiles = FillProxyPasswords(std::move(*profiles), *stored);
      loaded.kept_apart = true;
    }
  }
  loaded.profiles = std::move(*profiles);
  return loaded;
}

}  // namespace pane::model
