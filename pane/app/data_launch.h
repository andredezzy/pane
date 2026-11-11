// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_DATA_LAUNCH_H_
#define PANE_APP_DATA_LAUNCH_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback_forward.h"
#include "base/values.h"
#include "pane/model/data_key.h"
#include "pane/model/pin_entry.h"

namespace base {
class CommandLine;
class FilePath;
}  // namespace base

namespace pane {

namespace model {
class ModelStore;
}  // namespace model

// Pane's data around the engine's life (docs/plans/views-migration.md, C5),
// in the Swift app's order (app/Pane/main.swift, AppDelegate): what an
// earlier Pane left is brought up to date, and the data's lock taken, before
// the engine starts; the vault and the lock open once the thread pool runs;
// and the volume detaches once the engine has stopped. The Mac's alone: the
// encrypted volume and every earlier Pane's data are there (ADR 0113, 0114,
// 0122), and no Pane ever ran on Windows (U5).

// Pane's data folder: a scenario's (PANE_DATA_DIR), or xyz.roundedcorners.pane
// in the user's Application Support (ADR 0114).
base::FilePath DataDirectory();

// Before the engine starts, from PaneMainDelegate::BasicStartupComplete, with
// no thread pool yet: Chromium's InitializeUserDataDir, right after, makes the
// engine's folder, and an earlier Pane's data must have moved by then. An exit
// code ends the launch there: the Quit of the Electron migration's alert.
std::optional<int> PrepareData(const base::FilePath& data_directory,
                               const base::CommandLine& command_line);

// Once the browser has started, the thread pool running: the vault, the lock,
// and the model's store, with its settings. The lock starts with the UI
// (AppData::Start).
void OpenData();

// Once the browser's message loop has ended, while the thread pool still
// runs: what changed since the quit's save (a write that failed, asked again)
// is written, which the pool's shutdown waits for.
void DataWillStop();

// Once the engine has stopped (PaneMainDelegate::ProcessExiting), its files
// closed: the volume detaches, and Local State names Default again.
void DataDidStop();

// The data as Pane's UI sees it, from OpenData to DataDidStop: the lock, the
// data key it opens, what the key opens (the vault, on the Mac), and the
// model's store. On the window's sequence.
class AppData {
 public:
  // The UI, told what the data does.
  class Client {
   public:
    virtual ~Client() = default;

    // The lock opened, or the data did: once both have, what waited behind
    // them goes on (the Swift's accessDidOpen).
    virtual void AccessDidOpen() = 0;

    // The fifth wrong PIN in Settings: Pane locks.
    virtual void LockRequested() = 0;

    // The lock changed: a PIN set, changed, or removed (Settings' rows
    // follow), its screen shown, changed, or gone, Pane locked or open, or
    // the data's one-time move under way or done.
    virtual void SecurityChanged() = 0;

    // A wrong PIN: the dots shake, and the entry starts over 0.6 s on.
    virtual void PinShook() = 0;
  };

  static AppData* Get();

  virtual ~AppData() = default;

  // The UI shows: the lock starts, and without a PIN opens the data key at
  // once. `client` hears of the data until Stop.
  virtual void Start(Client* client) = 0;
  // The UI goes, as quitting starts.
  virtual void Stop() = 0;

  // The model, with its settings from the launch; its profiles once the key
  // opens.
  virtual model::ModelStore& store() = 0;

  // The PIN screen covers the window, and only the PIN opens it.
  virtual bool IsLocked() const = 0;
  // The profiles have loaded, and an earlier Pane's have moved in.
  virtual bool IsOpen() const = 0;
  // Only a PIN locks while open.
  virtual bool HasPin() const = 0;

  // A profile's engine key (ADR 0121), while the data key is open.
  virtual std::optional<model::Key> EngineKey(
      std::string_view profile_id) const = 0;
  // Runs `body` once the volume is attached, with false when it would not
  // attach, or a lock came first.
  virtual void WhenAttached(base::OnceCallback<void(bool)> body) = 0;
  // Brings the profile up to date before its context opens, once per open
  // (the vault's Prepare): `done` says whether it may open, and always runs.
  virtual void Prepare(const std::string& profile_id,
                       bool saves_history,
                       base::OnceCallback<void(bool)> done) = 0;
  // The profile came up in the engine.
  virtual void ProfileDidOpen(const std::string& profile_id) = 0;

  // A deleted profile is named in deletions.json before its data starts
  // going in the engine, so a quit, a crash, or a lock on the way leaves its
  // folder to the next open (ADR 0020): `recorded` runs once it is on the
  // disk.
  virtual void RecordDeletion(const std::string& profile_id,
                              base::OnceClosure recorded) = 0;
  // The engine marked it: its folder in the volume goes, then its record.
  virtual void RemoveDeleted(const std::string& profile_id) = 0;

  // Pane locks while open (ADR 0116): the PIN screen, at once.
  virtual void LockScreen() = 0;
  // Then, once what changed is written and the profiles have left the model,
  // the data closes, the key leaving memory; the volume detaches once the
  // returned callback says whether every profile closed in the engine. It
  // must run.
  [[nodiscard]] virtual base::OnceCallback<void(bool every_closed)>
  CloseData() = 0;

  // What the PIN screen shows (L1): its entry while it shows, the wrong
  // PINs at the lock in a row, on disk, and how far the data's one-time move
  // has come while it moves (Paper 07.5).
  virtual const std::optional<model::PinEntry>& PinScreen() const = 0;
  virtual int FailedAttempts() const = 0;
  virtual std::optional<double> DataMoveShare() const = 0;

  // The PIN screen: Settings' Set up, Change, and Remove PIN, and its keys,
  // which a PIN screen alone takes.
  virtual void ShowPinScreen(model::PinEntry::Mode mode) = 0;
  virtual void PinDigit(char16_t digit) = 0;
  virtual void PinBackspace() = 0;
  virtual void ContinueWithPin() = 0;
  virtual void DismissPinScreen() = 0;

  // The lock, the wipe, and the vault, as the channel reports them
  // (state.security).
  virtual base::DictValue State() const = 0;
};

}  // namespace pane

#endif  // PANE_APP_DATA_LAUNCH_H_
