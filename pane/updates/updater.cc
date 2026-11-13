// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/updater.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/common/chrome_paths.h"
#include "crypto/hash.h"
#include "net/base/load_flags.h"
#include "net/base/request_priority.h"
#include "net/http/http_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "pane/model/test_hooks.h"
#include "pane/model/update_install.h"
#include "pane/updates/dmg_download.h"
#include "pane/updates/update_files.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace pane {

namespace {

// GitHub's answer for the latest release is a few kilobytes.
constexpr size_t kMaxReleaseSize = 1024 * 1024;

constexpr net::NetworkTrafficAnnotationTag kUpdateCheck =
    net::DefineNetworkTrafficAnnotation("pane_update_check", R"(
      semantics {
        sender: "Pane's update check"
        description:
          "Asks the repo's GitHub Releases for the latest release, to offer "
          "a newer Pane."
        trigger: "At launch, every six hours, and Check for Updates."
        data: "None: a plain GET with Pane's user agent."
        destination: OTHER
        destination_other: "api.github.com"
      }
      policy {
        cookies_allowed: NO
        setting: "None."
        policy_exception_justification: "Pane has no enterprise policy."
      })");

// The download's progress, at most this often: once a frame.
constexpr base::TimeDelta kProgressInterval = base::Hertz(60);

// The file's SHA-256, in lowercase hex, read a megabyte at a time.
std::optional<std::string> Sha256Of(const base::FilePath& path) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    return std::nullopt;
  }
  crypto::hash::Hasher hasher(crypto::hash::kSha256);
  std::vector<uint8_t> chunk(1 << 20);
  while (true) {
    const std::optional<size_t> read = file.ReadAtCurrentPos(chunk);
    if (!read) {
      return std::nullopt;
    }
    if (*read == 0) {
      break;
    }
    hasher.Update(base::span(chunk).first(*read));
  }
  std::array<uint8_t, crypto::hash::kSha256Size> digest;
  hasher.Finish(digest);
  return base::HexEncodeLower(digest);
}

// The downloaded dmg, checked and saved: given the release's SHA-256, it
// must match, and leaves quarantine; one that differs is deleted. Without a
// digest it stays quarantined, and Gatekeeper asks before the app inside
// first opens. It goes into the downloads folder under its name, or with
// " (1)" and so on when that's taken, as Chrome names a download. Blocks.
std::optional<base::FilePath> CheckAndSave(base::FilePath downloaded,
                                           base::FilePath destination,
                                           std::optional<std::string> sha256) {
  if (sha256) {
    const std::optional<std::string> actual = Sha256Of(downloaded);
    if (actual != sha256) {
      LOG(ERROR) << "The update's dmg differs from the release's SHA-256";
      base::DeleteFile(downloaded);
      return std::nullopt;
    }
  }
  if (!base::CreateDirectory(destination.DirName())) {
    base::DeleteFile(downloaded);
    return std::nullopt;
  }
  base::FilePath saved = destination;
  for (int number = 1; base::PathExists(saved); ++number) {
    saved = destination.InsertBeforeExtensionASCII(
        base::StrCat({" (", base::NumberToString(number), ")"}));
  }
  if (!base::Move(downloaded, saved)) {
    base::DeleteFile(downloaded);
    return std::nullopt;
  }
  if (sha256) {
    ReleaseFromQuarantine(saved);
  }
  return saved;
}

}  // namespace

Updater::Config::Config() = default;
Updater::Config::Config(const Config&) = default;
Updater::Config& Updater::Config::operator=(const Config&) = default;
Updater::Config::~Config() = default;

// static
Updater::Config Updater::Config::FromEnvironment() {
  Config config;
  const std::optional<std::string> url = model::TestHook("PANE_UPDATE_URL");
  config.releases_url =
      url && GURL(*url).is_valid() ? GURL(*url) : model::ReleasesURL();
  if (const std::optional<std::string> downloads =
          model::TestHook("PANE_UPDATE_DOWNLOADS")) {
    config.downloads = base::FilePath::FromUTF8Unsafe(*downloads);
  } else {
    base::PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS, &config.downloads);
  }
  config.opens_download = model::TestHook("PANE_UPDATE_OPEN") != "0";
  config.installs = model::TestHook("PANE_UPDATE_INSTALL") != "0";
  // A scenario's copy opens the new Pane as a scenario's copy again.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("use-mock-keychain")) {
    config.relaunch.switches.push_back("--use-mock-keychain");
  }
  return config;
}

Updater::Updater(std::string current_version,
                 Config config,
                 scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
                 base::RepeatingClosure quit)
    : current_version_(std::move(current_version)),
      config_(std::move(config)),
      loader_factory_(std::move(loader_factory)),
      quit_(std::move(quit)),
      installer_(config_.installs ? UpdateInstaller::ForRunningApp()
                                  : std::nullopt),
      // It reads whole apps, and a quit never waits for it: what a staging
      // a quit cut short leaves, the next launch removes.
      install_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN})) {
  // What an earlier install left beside the app, off the launch's path.
  if (installer_) {
    install_runner_->PostTask(FROM_HERE,
                              base::BindOnce(
                                  [](const UpdateInstaller& installer) {
                                    installer.RemoveLeftovers();
                                  },
                                  *installer_));
  }
}

Updater::~Updater() = default;

std::u16string Updater::Label() const {
  switch (status_) {
    case Status::kDownloading:
      return u"Downloading update";
    case Status::kDownloaded:
      return staged() ? u"Restart to update" : u"Quit and install";
    default:
      return u"Update available";
  }
}

bool Updater::ShowsPill() const {
  return (status_ == Status::kAvailable || status_ == Status::kDownloading ||
          status_ == Status::kDownloaded) &&
         available_ && available_->version != dismissed_version_;
}

std::unique_ptr<network::ResourceRequest> Updater::RequestFor(
    const GURL& url) const {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  // Nothing of GitHub's kept: no cookie sent or saved, no cache (ADR 0113).
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->headers.SetHeader(net::HttpRequestHeaders::kUserAgent,
                             model::kUpdaterUserAgent);
  return request;
}

void Updater::Schedule() {
  schedule_timer_.Stop();
  Check(base::BindOnce(&Updater::Scheduled, weak_factory_.GetWeakPtr()));
}

void Updater::Scheduled(bool completed) {
  base::TimeDelta delay = model::kUpdateCheckInterval;
  if (completed) {
    retry_ = 0;
  } else {
    delay = model::kUpdateRetryDelays[std::min(
        retry_, model::kUpdateRetryDelays.size() - 1)];
    ++retry_;
  }
  schedule_timer_.Start(
      FROM_HERE, delay,
      base::BindOnce(&Updater::Schedule, base::Unretained(this)));
}

void Updater::Check(base::OnceCallback<void(bool)> done) {
  if (checking_ || status_ == Status::kDownloading) {
    if (done) {
      std::move(done).Run(true);
    }
    return;
  }
  checking_ = true;
  // Only a check with nothing on offer shows (Settings' Checking…).
  if (status_ == Status::kIdle) {
    SetStatus(Status::kChecking);
  }
  check_loader_ = network::SimpleURLLoader::Create(
      RequestFor(config_.releases_url), kUpdateCheck);
  check_loader_->DownloadToString(
      loader_factory_.get(),
      base::BindOnce(&Updater::Checked, weak_factory_.GetWeakPtr(),
                     std::move(done), base::TimeTicks::Now()),
      kMaxReleaseSize);
}

// Offline, rate-limited, or a bad answer: what was on offer stays. The
// checking state holds 0.8 s at least, so a click visibly does something.
void Updater::Checked(base::OnceCallback<void(bool)> done,
                      base::TimeTicks started,
                      std::optional<std::string> body) {
  const network::mojom::URLResponseHead* response =
      check_loader_->ResponseInfo();
  const int code =
      response && response->headers ? response->headers->response_code() : 0;
  check_loader_.reset();

  bool completed = false;
  std::optional<model::AvailableUpdate> update;
  if (code == 200 && body) {
    auto found =
        model::AvailableUpdateIn(*body, current_version_, config_.releases_url);
    completed = found.has_value();
    if (completed) {
      update = std::move(*found);
    }
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Updater::Settle, weak_factory_.GetWeakPtr(),
                     std::move(done), completed, std::move(update)),
      std::max(model::kMinimumChecking - (base::TimeTicks::Now() - started),
               base::TimeDelta()));
}

void Updater::Settle(base::OnceCallback<void(bool)> done,
                     bool completed,
                     std::optional<model::AvailableUpdate> update) {
  checking_ = false;
  if (completed) {
    last_checked_at_ = base::Time::Now();
    Offer(std::move(update));
  } else {
    LOG(ERROR) << "The update check failed";
    if (status_ == Status::kChecking) {
      SetStatus(Status::kIdle);
    }
  }
  if (done) {
    std::move(done).Run(completed);
  }
}

void Updater::Offer(std::optional<model::AvailableUpdate> update) {
  // Restart to update, once pressed, quits into what it named.
  if (status_ == Status::kDownloading || relaunches_) {
    return;
  }
  available_ = std::move(update);
  // A newer version than the one staged replaces it (ADR 0092): offered as
  // any is, it stages in turn once downloaded, and until then a quit
  // installs nothing.
  if (available_ && staged() && downloaded_version_ &&
      model::ReplacesStagedUpdate(available_->version, *downloaded_version_)) {
    Unstage();
  }
  if (!available_) {
    status_ = Status::kIdle;
  } else {
    status_ = available_->version == downloaded_version_ ? Status::kDownloaded
                                                         : Status::kAvailable;
  }
  Notify();
}

// On the installer's sequence, after the staging that made them and before
// the next; the dmg first, since a quit doesn't wait: a copy it leaves, the
// next launch removes (RemoveLeftovers).
void Updater::Unstage() {
  install_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](const base::FilePath& staged, const base::FilePath& dmg) {
            base::DeleteFile(dmg);
            base::DeletePathRecursively(staged);
          },
          std::exchange(staged_, base::FilePath()),
          std::exchange(downloaded_file_, base::FilePath())));
  downloaded_version_.reset();
}

void Updater::DismissPill() {
  dismissed_version_ = available_
                           ? std::optional<std::string>(available_->version)
                           : std::nullopt;
  Notify();
}

void Updater::DownloadUpdate() {
  // Restart to update: quits, and the quit swaps the staged Pane in and
  // opens it.
  if (status_ == Status::kDownloaded && staged()) {
    relaunches_ = true;
    quit_.Run();
    return;
  }
  // Downloaded already: its installer, as long as the dmg is still there.
  if (status_ == Status::kDownloaded && !downloaded_file_.empty()) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
        base::BindOnce(&base::PathExists, downloaded_file_),
        base::BindOnce(&Updater::QuitAndInstall, weak_factory_.GetWeakPtr()));
    return;
  }
  if ((status_ != Status::kAvailable && status_ != Status::kDownloaded) ||
      !available_) {
    return;
  }
  StartDownload(*available_);
}

void Updater::QuitAndInstall(bool exists) {
  if (!exists) {
    // Gone meanwhile: downloaded again.
    downloaded_file_.clear();
    downloaded_version_.reset();
    if (available_) {
      StartDownload(*available_);
    }
    return;
  }
  // An update Pane can't install itself (Paper 06.3.1): Pane quits, as the
  // copy in Applications can only be replaced once it has, and its installer
  // comes forward once it is gone. The user drags the new one in.
  OpenAfterExit(downloaded_file_);
  quit_.Run();
}

void Updater::StartDownload(model::AvailableUpdate update) {
  status_ = Status::kDownloading;
  download_progress_ = 0;
  download_eta_.reset();
  Notify();

  // The user's own request, its progress in view: the file it writes isn't
  // left to best-effort tasks, which Chromium holds until startup completes,
  // and a window with no page painted completes it only at a 3-minute
  // failsafe.
  std::unique_ptr<network::ResourceRequest> request =
      RequestFor(update.dmg_url);
  request->priority = net::LOW;
  download_ = std::make_unique<DmgDownload>(
      *request, loader_factory_,
      base::BindRepeating(&Updater::DownloadProgressed,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&Updater::Downloaded, weak_factory_.GetWeakPtr(),
                     std::move(update)));
}

void Updater::DownloadProgressed(double share, std::optional<int> seconds) {
  download_progress_ = share;
  download_eta_ = seconds;
  if (progress_timer_.IsRunning()) {
    progress_pending_ = true;
    return;
  }
  Notify();
  progress_timer_.Start(
      FROM_HERE, kProgressInterval,
      base::BindOnce(&Updater::ProgressSettled, base::Unretained(this)));
}

void Updater::ProgressSettled() {
  if (!progress_pending_) {
    return;
  }
  Notify();
  progress_timer_.Start(
      FROM_HERE, kProgressInterval,
      base::BindOnce(&Updater::ProgressSettled, base::Unretained(this)));
}

void Updater::Downloaded(model::AvailableUpdate update,
                         std::optional<base::FilePath> file) {
  download_.reset();
  download_eta_.reset();
  if (!file) {
    LOG(ERROR) << "The update download failed";
    download_progress_.reset();
    status_ = Status::kAvailable;
    Notify();
    return;
  }
  // The dmg under its own name, unescaped, as the Swift's lastPathComponent
  // read it: one file's, which the check made sure of
  // (model::IsTrustedDownload), and never a path.
  const base::FilePath destination = config_.downloads.Append(
      base::FilePath::FromUTF8Unsafe(
          base::UnescapeBinaryURLComponent(update.dmg_url.ExtractFileName()))
          .BaseName());
  // The digest reads the whole dmg: off the UI thread.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(&CheckAndSave, std::move(*file), destination,
                     update.sha256),
      base::BindOnce(&Updater::Verified, weak_factory_.GetWeakPtr(),
                     update.version));
}

// Saved and checked: its Pane is staged beside this one while the pill holds
// its full bar; without an installer the dmg opens for the user to drag, and
// the pill offers Quit and install. Refused: offered again.
void Updater::Verified(std::string version,
                       std::optional<base::FilePath> file) {
  if (!file) {
    download_progress_.reset();
    status_ = Status::kAvailable;
    Notify();
    return;
  }
  downloaded_version_ = version;
  downloaded_file_ = *file;
  if (!installer_) {
    download_progress_.reset();
    status_ = Status::kDownloaded;
    Notify();
    Open(*file);
    return;
  }
  download_progress_ = 1;
  Notify();
  install_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](const UpdateInstaller& installer, const base::FilePath& dmg,
             const std::string& version) {
            return installer.Stage(dmg, version);
          },
          *installer_, *file, std::move(version)),
      base::BindOnce(&Updater::Staged, weak_factory_.GetWeakPtr(), *file));
}

// Staged, the pill, Settings, and the Lock offer Restart to update; when a
// step failed, the dmg opens for the user to drag, as where Pane can't
// install it.
void Updater::Staged(base::FilePath file,
                     std::optional<base::FilePath> staged) {
  download_progress_.reset();
  status_ = Status::kDownloaded;
  if (staged) {
    staged_ = std::move(*staged);
  } else {
    LOG(ERROR) << "The update did not stage";
  }
  Notify();
  if (!staged) {
    Open(file);
  }
}

void Updater::InstallAtQuit() {
  if (!installer_ || !staged()) {
    return;
  }
  std::optional<UpdateInstaller::Relaunch> relaunch;
  if (relaunches_) {
    relaunch = config_.relaunch;
  }
  if (installer_->Install(staged_, downloaded_file_, relaunch)) {
    return;
  }
  LOG(ERROR) << "The update did not install";
  base::DeletePathRecursively(staged_);
  staged_.clear();
  if (!downloaded_file_.empty()) {
    OpenAfterExit(downloaded_file_);
  }
}

void Updater::Open(const base::FilePath& file) {
  if (config_.opens_download) {
    OpenUpdateFile(file);
  }
}

void Updater::OpenAfterExit(const base::FilePath& file) {
  if (config_.opens_download) {
    OpenUpdateFileAfterExit(file);
  }
}

void Updater::SetStatus(Status status) {
  if (status_ == status) {
    return;
  }
  status_ = status;
  Notify();
}

void Updater::Notify() {
  // What it tells carries the latest progress.
  progress_pending_ = false;
  for (Observer& observer : observers_) {
    observer.OnUpdaterChanged();
  }
}

void Updater::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void Updater::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

std::string_view UpdateStatusName(Updater::Status status) {
  switch (status) {
    case Updater::Status::kIdle:
      return "idle";
    case Updater::Status::kChecking:
      return "checking";
    case Updater::Status::kAvailable:
      return "available";
    case Updater::Status::kDownloading:
      return "downloading";
    case Updater::Status::kDownloaded:
      return "downloaded";
  }
}

}  // namespace pane
