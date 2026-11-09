// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/panic_wipe_run.h"

#include <crt_externs.h>
#include <libproc.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <string_view>
#include <utility>

#include "base/base_paths.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "pane/model/mac/data_vault.h"
#include "pane/model/mac/electron_migration.h"
#include "pane/model/mac/proxy_password_store.h"
#include "pane/model/persisted_file.h"
#include "pane/model/security.h"

namespace pane::model {

namespace {

// How long the instance a wipe started waits for the wiped one to go.
constexpr base::TimeDelta kWipedWait = base::Seconds(30);

// For the log: how many, never their paths.
void LogFailures(const std::vector<WipeFailure>& failures) {
  if (!failures.empty()) {
    LOG(ERROR) << "The panic wipe could not erase " << failures.size()
               << " items";
  }
}

std::vector<std::string> Described(const std::vector<WipeFailure>& failures) {
  std::vector<std::string> described;
  for (const WipeFailure& failure : failures) {
    described.push_back(failure.ToString());
  }
  return described;
}

// "--pane-wipe-after=".
std::string WipeAfterPrefix() {
  return base::StrCat({"--", kWipeAfterSwitch, "="});
}

// What this instance was started with, as the system gave it, as the Swift's
// CommandLine.arguments: the command line Chromium reads carries the switches
// Pane's launch adds to it (SetEngineSwitches, --pane-hosted among them), and
// an instance started with those runs as a hosted engine, never Pane's launch,
// which erases.
std::vector<std::string> StartedWith() {
  // SAFETY: the system gives `*_NSGetArgc()` arguments in `*_NSGetArgv()`.
  const base::span<char*> arguments = UNSAFE_BUFFERS(
      base::span(*_NSGetArgv(), static_cast<size_t>(*_NSGetArgc())));
  return std::vector<std::string>(arguments.begin(), arguments.end());
}

// The pids the wiped instance's switch names, read as the Swift read them:
// the first argument it starts, wherever it is (base::CommandLine stops at
// "--"), and what is no number goes. nullopt without one.
std::optional<std::vector<base::ProcessId>> WipedProcesses(
    const base::CommandLine::StringVector& argv) {
  const std::string prefix = WipeAfterPrefix();
  const auto argument =
      std::ranges::find_if(argv, [&](const std::string& argument) {
        return argument.starts_with(prefix);
      });
  if (argument == argv.end()) {
    return std::nullopt;
  }
  std::vector<base::ProcessId> pids;
  for (std::string_view part : base::SplitStringPiece(
           std::string_view(*argument).substr(prefix.size()), ",",
           base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    int pid = 0;
    if (base::StringToInt(part, &pid)) {
      pids.push_back(pid);
    }
  }
  return pids;
}

// The erase at launch, which takes as long as the data is large.
class EraseThread : public base::PlatformThread::Delegate {
 public:
  EraseThread(scoped_refptr<LaunchErase> erase,
              const base::FilePath& data_directory,
              const std::optional<base::FilePath>& cache_directory,
              const std::optional<ElectronData>& electron,
              SafeStorage safe_storage)
      : erase_(std::move(erase)),
        data_directory_(data_directory),
        cache_directory_(cache_directory),
        electron_(electron),
        safe_storage_(safe_storage) {}

  void ThreadMain() override {
    base::PlatformThread::SetName("PanicWipeErase");
    const base::TimeTicks started = base::TimeTicks::Now();
    const std::vector<WipeFailure> failures =
        EraseStagedWipes(data_directory_, cache_directory_, electron_);
    // Here, off the launch: the Keychain may take its time.
    ForgetElectronKey(electron_, safe_storage_);
    VLOG(1) << "The panic wipe erased its data in "
            << (base::TimeTicks::Now() - started).InSecondsF() << " s";
    LogFailures(failures);
    erase_->Finish(Described(failures));
    delete this;
  }

 private:
  const scoped_refptr<LaunchErase> erase_;
  const base::FilePath data_directory_;
  const std::optional<base::FilePath> cache_directory_;
  const std::optional<ElectronData> electron_;
  const SafeStorage safe_storage_;
};

}  // namespace

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

LaunchErase::LaunchErase() = default;
LaunchErase::~LaunchErase() = default;

bool LaunchErase::IsErasing() const {
  base::AutoLock lock(lock_);
  return erasing_;
}

std::vector<std::string> LaunchErase::Failures() const {
  base::AutoLock lock(lock_);
  return failures_;
}

void LaunchErase::Wait() {
  done_.Wait();
}

void LaunchErase::Finish(std::vector<std::string> failures) {
  {
    base::AutoLock lock(lock_);
    failures_ = std::move(failures);
    erasing_ = false;
  }
  done_.Signal();
}

scoped_refptr<LaunchErase> PanicWipeAtLaunch(
    const base::FilePath& data_directory,
    const base::CommandLine& command_line,
    VolumeTools& tools,
    SafeStorage safe_storage) {
  const std::optional<base::FilePath> caches =
      ProfileCacheDirectory(data_directory);
  const std::optional<ElectronData> electron = ElectronDataFor(data_directory);
  const std::optional<std::vector<base::ProcessId>> wiped =
      WipedProcesses(command_line.argv());

  if (wiped) {
    const base::TimeTicks deadline = base::TimeTicks::Now() + kWipedWait;
    while (std::ranges::any_of(
               *wiped, [](base::ProcessId pid) { return kill(pid, 0) == 0; }) &&
           base::TimeTicks::Now() < deadline) {
      base::PlatformThread::Sleep(base::Milliseconds(50));
    }
  }

  if (wiped || IsWipeDue(data_directory)) {
    ProxyPasswordStore(data_directory, safe_storage).Delete();
    // The wiped instance's volume, which its watchdog went down with: its key
    // is gone, and it moves aside detached.
    tools.DetachLeftovers();
    LogFailures(StageWipe(data_directory, caches, electron));
  }

  if (StagedWipes(data_directory, caches, electron).empty()) {
    return nullptr;
  }

  auto erase = base::MakeRefCounted<LaunchErase>();
  if (!base::PlatformThread::CreateNonJoinableWithType(
          0,
          new EraseThread(erase, data_directory, caches, electron,
                          safe_storage),
          base::ThreadType::kUtility)) {
    LOG(ERROR) << "Could not start the panic wipe's erase";
    erase->Finish({});
  }
  return erase;
}

bool IsWipeDue(const base::FilePath& data_directory) {
  const PersistedFile file(data_directory, "security");
  std::optional<SecurityState> state;
  if (const base::expected<std::optional<base::Value>, FileError> loaded =
          file.Load();
      loaded.has_value() && *loaded) {
    state = SecurityState::FromValue(**loaded);
  }
  if (!state) {
    std::string text;
    if (base::ReadFileToString(file.path(), &text)) {
      state = SecurityState::Recover(text);
    }
  }
  return state && state->pin &&
         state->failed_attempts >= SecurityState::kMaxAttempts && !state->key;
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

std::vector<base::ProcessId> ChildProcesses() {
  const uint32_t parent = static_cast<uint32_t>(getpid());
  const int size = proc_listpids(PROC_PPID_ONLY, parent, nullptr, 0);
  if (size <= 0) {
    return {};
  }
  std::vector<pid_t> pids(static_cast<size_t>(size) / sizeof(pid_t) + 16);
  const int filled =
      proc_listpids(PROC_PPID_ONLY, parent, pids.data(),
                    static_cast<int>(pids.size() * sizeof(pid_t)));
  pids.resize(std::min(
      pids.size(), static_cast<size_t>(std::max(0, filled)) / sizeof(pid_t)));
  std::erase_if(pids, [](pid_t pid) { return pid <= 0; });
  return pids;
}

bool Relaunch(const std::vector<base::ProcessId>& processes) {
  base::FilePath executable;
  if (!base::PathService::Get(base::FILE_EXE, &executable)) {
    return false;
  }

  const std::string wiped = WipeAfterPrefix();
  const std::vector<std::string> current = StartedWith();
  std::vector<std::string> argv = {executable.value()};
  for (const std::string& argument : base::span(current).subspan(1u)) {
    if (!argument.starts_with(wiped)) {
      argv.push_back(argument);
    }
  }
  std::vector<std::string> pids;
  for (base::ProcessId pid : processes) {
    pids.push_back(base::NumberToString(pid));
  }
  argv.push_back(base::StrCat({wiped, base::JoinString(pids, ",")}));

  if (!base::LaunchProcess(argv, base::LaunchOptions()).IsValid()) {
    LOG(ERROR) << "Could not start again after the wipe";
    return false;
  }
  return true;
}

void WipeAndRelaunch(const base::FilePath& data_directory,
                     VolumeTools& tools,
                     SafeStorage safe_storage) {
  const std::vector<base::ProcessId> helpers = ChildProcesses();
  for (base::ProcessId helper : helpers) {
    kill(helper, SIGKILL);
  }

  std::vector<base::ProcessId> processes = {base::GetCurrentProcId()};
  processes.insert(processes.end(), helpers.begin(), helpers.end());
  if (!Relaunch(processes)) {
    // No instance to erase: this one does, its engine still running.
    const std::optional<base::FilePath> caches =
        ProfileCacheDirectory(data_directory);
    const std::optional<ElectronData> electron =
        ElectronDataFor(data_directory);
    tools.DetachLeftovers();
    ProxyPasswordStore(data_directory, safe_storage).Delete();
    ForgetElectronKey(electron, safe_storage);
    std::vector<WipeFailure> failures =
        StageWipe(data_directory, caches, electron);
    std::ranges::move(EraseStagedWipes(data_directory, caches, electron),
                      std::back_inserter(failures));
    LogFailures(failures);
  }

  _exit(0);
}

}  // namespace pane::model
