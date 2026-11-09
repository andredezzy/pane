// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MODEL_STORE_H_
#define PANE_MODEL_MODEL_STORE_H_

#include <map>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/files/important_file_writer.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "pane/model/app_settings.h"
#include "pane/model/browser_model.h"
#include "pane/model/data_key.h"

namespace base {
class Clock;
}  // namespace base

namespace pane::model {

// Where BrowserModel's state lives (the Swift app's ModelStore): profiles.json,
// sealed under the data key's files key with each proxy's password in it (ADR
// 0113), and settings.json, plain, since the lock's theme and the Dock's icon
// show before the PIN, in the data directory. The model starts with its
// settings; the profiles come once the key opens. An earlier Pane's plain
// profiles.json, and the proxy passwords it kept apart, are read once then,
// and sealed.
//
// It owns the model, and lives on the window's sequence. The files are read
// and written on a sequence of their own, in the order asked, which a quit
// waits for (BLOCK_SHUTDOWN): the JSON, AES-GCM, and base64 never run on the
// window's sequence, where Views composites (docs/plans/views-migration.md,
// C3). The model already waits 300 ms after a change, so each save goes to
// its file's ImportantFileWriter at once.
class ModelStore {
 public:
  // The data key's files key while it is open: nullopt before it opens and
  // once a lock closes it, when no save writes over profiles.json.
  using FilesKey = base::RepeatingCallback<std::optional<Key>()>;

  // Proxy passwords by profile id.
  using Passwords = std::map<std::string, std::string>;

  // Where an earlier Pane kept the proxy passwords apart from profiles.json
  // (the Mac's ProxyPasswordStore): read when its plain profiles.json is, for
  // a proxy that signs in, and removed once the profiles are sealed with
  // them. Both run on the store's sequence, since the Keychain may ask the
  // user first. Empty, there is nowhere to look.
  struct KeptApart {
    KeptApart();
    KeptApart(const KeptApart&);
    KeptApart& operator=(const KeptApart&);
    ~KeptApart();

    // nullopt when they could not be read (the user denied the key, say).
    base::RepeatingCallback<std::optional<Passwords>()> read;
    base::RepeatingClosure remove;
  };

  // settings.json, before the model exists: blocks on the disk. A file that
  // doesn't read is kept as settings.json.damaged, before a save writes over
  // it, and the defaults stand.
  static AppSettings LoadSettings(const base::FilePath& data_directory);

  // `clock` stamps the profiles and outlives the store.
  ModelStore(const base::FilePath& data_directory,
             AppSettings settings,
             const base::Clock* clock,
             FilesKey files_key,
             KeptApart kept_apart);
  ModelStore(const ModelStore&) = delete;
  ModelStore& operator=(const ModelStore&) = delete;
  ~ModelStore();

  BrowserModel& model() { return model_; }
  const BrowserModel& model() const { return model_; }

  // Once the data key opens: profiles.json, read and opened off this
  // sequence, goes into the model, then `loaded` runs. One that neither opens
  // nor reads as an earlier Pane's plain file is kept as
  // profiles.json.damaged, and the model has none. A plain one is read once,
  // with the passwords kept apart, and sealed at once: on the first open, or
  // on one after a quit cut that open short. One the disk won't give leaves
  // the model without profiles, and nothing is saved over it until a load
  // reads it. A lock before then (UnloadProfiles) leaves both out.
  void LoadProfiles(base::OnceClosure loaded);

  // Pane locked while open (ADR 0116): what changed is written, then the
  // profiles leave the model, until the key opens them again. Until they
  // have loaded no save writes over profiles.json, which would lose every
  // profile the model doesn't hold yet.
  void UnloadProfiles();

  // Writes what changed, and runs `written` once it is on the disk: at quit,
  // and for scenarios that read the files.
  void SaveNow(base::OnceClosure written);

  bool profiles_loaded() const { return profiles_loaded_; }

 private:
  // Hands a writer the file's text to make on the store's sequence.
  class PendingWrite
      : public base::ImportantFileWriter::BackgroundDataSerializer {
   public:
    PendingWrite();
    ~PendingWrite() override;

    void Set(base::ImportantFileWriter::BackgroundDataProducerCallback make);

    // base::ImportantFileWriter::BackgroundDataSerializer:
    base::ImportantFileWriter::BackgroundDataProducerCallback
    GetSerializedDataProducerForBackgroundSequence() override;

   private:
    base::ImportantFileWriter::BackgroundDataProducerCallback make_;
  };

  // What a load read.
  struct Loaded;

  // The Swift's loadProfiles, on the store's sequence: profiles.json opened
  // with the files key; an earlier Pane's plain one read once, with the
  // passwords it kept apart; one that does neither kept aside.
  static Loaded ReadProfiles(
      const base::FilePath& directory,
      const Key& key,
      base::RepeatingCallback<std::optional<Passwords>()> read_kept_apart);

  void Persist(BrowserModel::Save save);
  void Write(base::ImportantFileWriter& writer,
             PendingWrite& pending,
             base::ImportantFileWriter::BackgroundDataProducerCallback make,
             base::OnceCallback<void(bool)> after);
  void DidLoad(int loads, base::OnceClosure loaded, Loaded result);
  void Written(BrowserModel::File file, bool kept_apart, bool written);

  const base::FilePath data_directory_;
  const FilesKey files_key_;
  const KeptApart kept_apart_;
  const scoped_refptr<base::SequencedTaskRunner> files_;
  // Before the writers, which point at them until each write starts.
  PendingWrite pending_profiles_;
  PendingWrite pending_settings_;
  base::ImportantFileWriter profiles_writer_;
  base::ImportantFileWriter settings_writer_;
  // After the writers: its saves reach them.
  BrowserModel model_;

  // The model holds what profiles.json does: only then is it written.
  bool profiles_loaded_ = false;
  // The passwords an earlier Pane kept apart are in the model: their store
  // goes once the profiles are sealed with them.
  bool passwords_kept_apart_ = false;
  // Locks so far: a load a lock overtook loads nothing.
  int loads_ = 0;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<ModelStore> weak_factory_{this};
};

}  // namespace pane::model

#endif  // PANE_MODEL_MODEL_STORE_H_
