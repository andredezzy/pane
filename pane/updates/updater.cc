// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/updater.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

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

constexpr net::NetworkTrafficAnnotationTag kUpdateDownload =
    net::DefineNetworkTrafficAnnotation("pane_update_download", R"(
      semantics {
        sender: "Pane's update download"
        description: "Downloads the offered release's dmg."
        trigger: "The user's click on the update pill or in Settings."
        data: "None: a plain GET with Pane's user agent."
        destination: OTHER
        destination_other: "github.com"
      }
      policy {
        cookies_allowed: NO
        setting: "None."
        policy_exception_justification: "Pane has no enterprise policy."
      })");

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
  return config;
}

Updater::Updater(std::string current_version,
                 Config config,
                 scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
                 base::RepeatingClosure quit)
    : current_version_(std::move(current_version)),
      config_(std::move(config)),
      loader_factory_(std::move(loader_factory)),
      quit_(std::move(quit)) {}

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
  if (status_ == Status::kDownloading) {
    return;
  }
  available_ = std::move(update);
  if (!available_) {
    status_ = Status::kIdle;
  } else {
    status_ = available_->version == downloaded_version_ ? Status::kDownloaded
                                                         : Status::kAvailable;
  }
  Notify();
}

void Updater::DismissPill() {
  dismissed_version_ = available_
                           ? std::optional<std::string>(available_->version)
                           : std::nullopt;
  Notify();
}

void Updater::DownloadUpdate() {
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
  // An update Pane can't install itself (Paper 06.3.1): its installer comes
  // forward, and Pane quits, as the copy in Applications can only be
  // replaced once it has. The user drags the new one in.
  Open(downloaded_file_);
  quit_.Run();
}

void Updater::StartDownload(model::AvailableUpdate update) {
  status_ = Status::kDownloading;
  download_progress_ = 0;
  download_eta_.reset();
  download_total_ = -1;
  eta_.emplace();
  Notify();

  // The user's own request, its progress in view: the file it writes isn't
  // left to best-effort tasks, which Chromium holds until startup completes,
  // and a window with no page painted completes it only at a 3-minute
  // failsafe.
  std::unique_ptr<network::ResourceRequest> request =
      RequestFor(update.dmg_url);
  request->priority = net::LOW;
  download_loader_ =
      network::SimpleURLLoader::Create(std::move(request), kUpdateDownload);
  download_loader_->SetOnResponseStartedCallback(base::BindOnce(
      [](base::WeakPtr<Updater> updater, const GURL& final_url,
         const network::mojom::URLResponseHead& head) {
        if (updater) {
          updater->DownloadStarted(head.content_length);
        }
      },
      weak_factory_.GetWeakPtr()));
  download_loader_->SetOnDownloadProgressCallback(base::BindRepeating(
      &Updater::DownloadProgressed, weak_factory_.GetWeakPtr()));
  download_loader_->DownloadToTempFile(
      loader_factory_.get(),
      base::BindOnce(&Updater::Downloaded, weak_factory_.GetWeakPtr(),
                     std::move(update)));
}

void Updater::DownloadStarted(int64_t total) {
  download_total_ = total;
}

void Updater::DownloadProgressed(uint64_t received) {
  if (download_total_ <= 0) {
    return;
  }
  const int64_t bytes = static_cast<int64_t>(received);
  download_progress_ =
      std::min(static_cast<double>(bytes) / download_total_, 1.0);
  download_eta_ = eta_->Update(bytes, download_total_, base::Time::Now());
  Notify();
}

void Updater::Downloaded(model::AvailableUpdate update, base::FilePath file) {
  const int error = download_loader_->NetError();
  download_loader_.reset();
  download_eta_.reset();
  eta_.reset();
  if (file.empty() || error != 0) {
    LOG(ERROR) << "The update download failed: " << error;
    if (!file.empty()) {
      base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()},
                                 base::GetDeleteFileCallback(file));
    }
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
      base::BindOnce(&CheckAndSave, std::move(file), destination,
                     update.sha256),
      base::BindOnce(&Updater::Verified, weak_factory_.GetWeakPtr(),
                     update.version));
}

// Saved: without the installer (K1) the dmg opens for the user to drag, and
// the pill offers Quit and install. Refused: offered again.
void Updater::Verified(std::string version,
                       std::optional<base::FilePath> file) {
  download_progress_.reset();
  if (!file) {
    status_ = Status::kAvailable;
    Notify();
    return;
  }
  downloaded_version_ = std::move(version);
  downloaded_file_ = *file;
  status_ = Status::kDownloaded;
  Notify();
  Open(*file);
}

void Updater::Open(const base::FilePath& file) {
  if (config_.opens_download) {
    OpenUpdateFile(file);
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
