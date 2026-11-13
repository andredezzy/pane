// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/panic_wipe_run.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
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

// An argument as UTF-8, and ASCII added to one, whichever string a system's
// command lines hold: the Mac's bytes, Windows' UTF-16.
template <typename String>
std::string Utf8(const String& argument) {
  if constexpr (std::is_same_v<typename String::value_type, char>) {
    return std::string(argument);
  } else {
    return base::WideToUTF8(argument);
  }
}

template <typename String>
void AppendAscii(String& argument, std::string_view ascii) {
  argument.append(ascii.begin(), ascii.end());
}

// The pids the wiped instance's switch names, read as the Swift read them:
// the first argument it starts, wherever it is (base::CommandLine stops at
// "--"), and what is no number goes. nullopt without one.
std::optional<std::vector<base::ProcessId>> WipedProcesses(
    const base::CommandLine::StringVector& argv) {
  const std::string prefix = WipeAfterPrefix();
  const auto argument = std::ranges::find_if(
      argv, [&](const base::CommandLine::StringType& argument) {
        return Utf8(argument).starts_with(prefix);
      });
  if (argument == argv.end()) {
    return std::nullopt;
  }
  std::vector<base::ProcessId> pids;
  const std::string named = Utf8(*argument).substr(prefix.size());
  for (std::string_view part : base::SplitStringPiece(
           named, ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    int pid = 0;
    if (base::StringToInt(part, &pid)) {
      pids.push_back(static_cast<base::ProcessId>(pid));
    }
  }
  return pids;
}

// The erase at launch, which takes as long as the data is large.
class EraseThread : public base::PlatformThread::Delegate {
 public:
  EraseThread(scoped_refptr<LaunchErase> erase,
              const base::FilePath& data_directory,
              const WipeExtras& extras)
      : erase_(std::move(erase)),
        data_directory_(data_directory),
        extras_(extras) {}

  void ThreadMain() override {
    base::PlatformThread::SetName("PanicWipeErase");
    const base::TimeTicks started = base::TimeTicks::Now();
    const std::vector<WipeFailure> failures = EraseStagedWipes(
        data_directory_, extras_.cache_directory, extras_.electron);
    // Here, off the launch: the Keychain may take its time.
    if (extras_.off_the_launch) {
      extras_.off_the_launch.Run();
    }
    VLOG(1) << "The panic wipe erased its data in "
            << (base::TimeTicks::Now() - started).InSecondsF() << " s";
    LogFailures(failures);
    erase_->Finish(Described(failures));
    delete this;
  }

 private:
  const scoped_refptr<LaunchErase> erase_;
  const base::FilePath data_directory_;
  const WipeExtras extras_;
};

}  // namespace

WipeExtras::WipeExtras() = default;
WipeExtras::WipeExtras(const WipeExtras&) = default;
WipeExtras& WipeExtras::operator=(const WipeExtras&) = default;
WipeExtras::~WipeExtras() = default;

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
    const WipeExtras& extras) {
  const std::optional<std::vector<base::ProcessId>> wiped =
      WipedProcesses(command_line.argv());

  if (wiped) {
    WaitForProcesses(*wiped, kWipedWait);
  }

  if (wiped || IsWipeDue(data_directory)) {
    if (extras.at_once) {
      extras.at_once.Run();
    }
    LogFailures(
        StageWipe(data_directory, extras.cache_directory, extras.electron));
  }

  if (StagedWipes(data_directory, extras.cache_directory, extras.electron)
          .empty()) {
    return nullptr;
  }

  auto erase = base::MakeRefCounted<LaunchErase>();
  if (!base::PlatformThread::CreateNonJoinableWithType(
          0, new EraseThread(erase, data_directory, extras),
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

bool Relaunch(const std::vector<base::ProcessId>& processes) {
  const std::string wiped = WipeAfterPrefix();
  const base::CommandLine::StringVector current = StartedWith();
  if (current.empty()) {
    return false;
  }
  base::CommandLine::StringVector argv = {current.front()};
  for (const base::CommandLine::StringType& argument :
       base::span(current).subspan(1u)) {
    if (!Utf8(argument).starts_with(wiped)) {
      argv.push_back(argument);
    }
  }
  std::vector<std::string> pids;
  for (base::ProcessId pid : processes) {
    pids.push_back(base::NumberToString(pid));
  }
  base::CommandLine::StringType named;
  AppendAscii(named, base::StrCat({wiped, base::JoinString(pids, ",")}));
  argv.push_back(std::move(named));

  if (!Start(argv)) {
    LOG(ERROR) << "Could not start again after the wipe";
    return false;
  }
  return true;
}

void WipeAndRelaunch(const base::FilePath& data_directory,
                     const WipeExtras& extras) {
  const std::vector<base::ProcessId> helpers = ChildProcesses();
  KillProcesses(helpers);

  std::vector<base::ProcessId> processes = {base::GetCurrentProcId()};
  processes.insert(processes.end(), helpers.begin(), helpers.end());
  if (!Relaunch(processes)) {
    // No instance to erase: this one does, its engine still running.
    if (extras.at_once) {
      extras.at_once.Run();
    }
    if (extras.off_the_launch) {
      extras.off_the_launch.Run();
    }
    std::vector<WipeFailure> failures =
        StageWipe(data_directory, extras.cache_directory, extras.electron);
    std::ranges::move(EraseStagedWipes(data_directory, extras.cache_directory,
                                       extras.electron),
                      std::back_inserter(failures));
    LogFailures(failures);
  }

  EndNow();
}

}  // namespace pane::model
