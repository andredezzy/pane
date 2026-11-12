// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/data_launch.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/path_service.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/default_clock.h"
#include "pane/model/app_settings.h"
#include "pane/model/data_key.h"
#include "pane/model/local_state.h"
#include "pane/model/model_store.h"

namespace pane {

// Pane's data on Windows, its folder, the data key under DPAPI, the PIN, and
// the lock, is W3's (docs/plans/views-migration.md, U5). Until then a launch
// on the user's data ends at once, and a test copy's data lasts as long as
// its process: a data key made at launch and sealed nowhere, no PIN, no lock.

namespace {

base::FilePath EngineDirectory(const base::FilePath& data_directory) {
  return data_directory.Append(FILE_PATH_LITERAL("Engine"));
}

// What the launch kept for the data, which opens once the thread pool runs.
struct Launched {
  base::FilePath data_directory;
  model::AppSettings settings;
};

Launched* g_launched = nullptr;

// A test copy's data: the model's store in its folder, profiles.json sealed
// under the files key of a data key no file keeps, and each profile's engine
// key from it.
class TestCopyData : public AppData {
 public:
  explicit TestCopyData(Launched launched)
      : data_directory_(launched.data_directory),
        data_key_(model::MakeDataKey()),
        store_(launched.data_directory,
               std::move(launched.settings),
               base::DefaultClock::GetInstance(),
               base::BindRepeating(&TestCopyData::FilesKey,
                                   base::Unretained(this)),
               model::ModelStore::KeptApart()) {}
  TestCopyData(const TestCopyData&) = delete;
  TestCopyData& operator=(const TestCopyData&) = delete;
  ~TestCopyData() override = default;

  const base::FilePath& data_directory() const { return data_directory_; }

  // AppData:
  void Start(Client* client) override {
    client_ = client;
    // No PIN: the profiles load at once, as the Mac's do without one.
    store_.LoadProfiles(
        base::BindOnce(&TestCopyData::Loaded, weak_factory_.GetWeakPtr()));
  }

  void Stop() override { client_ = nullptr; }

  model::ModelStore& store() override { return store_; }

  bool IsLocked() const override { return false; }
  bool IsOpen() const override { return open_; }
  bool HasPin() const override { return false; }

  std::optional<model::Key> EngineKey(
      std::string_view profile_id) const override {
    return model::EngineFilesKey(data_key_, profile_id);
  }

  // No volume to attach, and no earlier Pane's files to bring up to date.
  void WhenAttached(base::OnceCallback<void(bool)> body) override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(body), true));
  }

  void Prepare(const std::string& profile_id,
               bool saves_history,
               base::OnceCallback<void(bool)> done) override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(done), true));
  }

  void ProfileDidOpen(const std::string& profile_id) override {}

  // deletions.json and a deleted profile's folder are W3's.
  void RecordDeletion(const std::string& profile_id,
                      base::OnceClosure recorded) override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(recorded));
  }

  void RemoveDeleted(const std::string& profile_id) override {}

  // The PIN and the lock are W3's: with no PIN, nothing locks.
  void LockScreen() override {}

  base::OnceCallback<void(bool)> CloseData() override {
    return base::DoNothing();
  }

  const std::optional<model::PinEntry>& PinScreen() const override {
    return pin_screen_;
  }

  int FailedAttempts() const override { return 0; }

  std::optional<double> DataMoveShare() const override { return std::nullopt; }

  void ShowPinScreen(model::PinEntry::Mode mode) override {}
  void PinDigit(char16_t digit) override {}
  void PinBackspace() override {}
  void ContinueWithPin() override {}
  void DismissPinScreen() override {}

  base::DictValue State() const override {
    return base::DictValue()
        .Set("pinLength", base::Value())
        .Set("failedAttempts", 0)
        .Set("locked", false)
        .Set("wipe", base::DictValue()
                         .Set("erasing", false)
                         .Set("failures", base::ListValue()))
        .Set("data", base::DictValue()
                         .Set("phase", open_ ? "open" : "closed")
                         .Set("attached", false)
                         .Set("failure", base::Value())
                         .Set("attachedAt", base::Value()))
        .Set("screen", base::Value());
  }

 private:
  std::optional<model::Key> FilesKey() const {
    return model::FilesKey(data_key_);
  }

  void Loaded() {
    open_ = true;
    if (client_) {
      client_->AccessDidOpen();
    }
  }

  const base::FilePath data_directory_;
  const model::Key data_key_;
  raw_ptr<Client> client_ = nullptr;
  bool open_ = false;
  const std::optional<model::PinEntry> pin_screen_;
  // After the key, whose files key it reads.
  model::ModelStore store_;
  base::WeakPtrFactory<TestCopyData> weak_factory_{this};
};

TestCopyData* g_data = nullptr;

}  // namespace

base::FilePath DefaultDataDirectory() {
  base::FilePath local_app_data;
  base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local_app_data);
  return local_app_data.Append(FILE_PATH_LITERAL("Pane"));
}

std::optional<int> PrepareData(const base::FilePath& data_directory,
                               const base::CommandLine& command_line,
                               bool test_copy) {
  CHECK(!g_launched);
  if (!test_copy) {
    LOG(ERROR) << "Pane opens no data of the user's on Windows yet (W3)";
    return 1;
  }
  if (!base::CreateDirectory(data_directory)) {
    LOG(ERROR) << "Could not make a test copy's data folder";
    return 1;
  }
  // Chromium opens the profile it used last at startup, before Pane gives it
  // its key: never one of Pane's.
  if (!model::StartOnDefaultProfile(EngineDirectory(data_directory))) {
    LOG(ERROR) << "Could not point Local State at the Default profile";
  }
  g_launched =
      new Launched{.data_directory = data_directory,
                   .settings = model::ModelStore::LoadSettings(data_directory)};
  return std::nullopt;
}

void OpenData() {
  CHECK(g_launched);
  CHECK(!g_data);
  const std::unique_ptr<Launched> launched(std::exchange(g_launched, nullptr));
  g_data = new TestCopyData(std::move(*launched));
}

// static
AppData* AppData::Get() {
  return g_data;
}

void DataWillStop() {
  if (g_data) {
    g_data->store().model().SaveNow();
  }
}

void DataDidStop() {
  base::FilePath data_directory;
  if (g_launched) {
    const std::unique_ptr<Launched> launched(
        std::exchange(g_launched, nullptr));
    data_directory = launched->data_directory;
  }
  if (g_data) {
    data_directory = g_data->data_directory();
    delete std::exchange(g_data, nullptr);
  }
  // Chromium wrote it as it quit: the profile used last.
  if (!data_directory.empty() &&
      !model::StartOnDefaultProfile(EngineDirectory(data_directory))) {
    LOG(ERROR) << "Could not point Local State at the Default profile";
  }
}

}  // namespace pane
