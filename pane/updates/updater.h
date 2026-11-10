// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UPDATES_UPDATER_H_
#define PANE_UPDATES_UPDATER_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "pane/model/updates.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
struct ResourceRequest;
}  // namespace network

namespace pane {

// The update check and the download (the Swift's Updater, ADR 0002;
// DESIGN.md › Updates; CONTEXT.md: update check): the repo's latest GitHub
// Release at launch and every six hours, with retries after a failure; a
// click downloads its dmg to Downloads, which, once it checks out against the
// release's SHA-256, leaves quarantine. Never a modal, never a forced
// restart. The pill, Settings, and the Lock read this one source.
//
// Staging the new version beside the app, the swap at quit, and Restart to
// update are the installer's (UpdateInstaller, ADR 0092), which K1 ports
// with the download's connection rules (DmgDownload): until then a download
// is Quit and install, the dmg opening for the user to drag, as the Swift's
// was where Pane can't install itself.
class Updater {
 public:
  enum class Status {
    kIdle,
    kChecking,
    kAvailable,
    kDownloading,
    kDownloaded,
  };

  class Observer : public base::CheckedObserver {
   public:
    // Anything the pill, Settings, or the Lock shows changed.
    virtual void OnUpdaterChanged() = 0;
  };

  // Where it looks and saves, and whether it opens what it saved: GitHub's
  // latest release, the user's Downloads, and yes, unless a scenario's copy
  // (the mock keychain) points them elsewhere (PANE_UPDATE_URL,
  // PANE_UPDATE_DOWNLOADS, PANE_UPDATE_OPEN=0), as the Swift's TestHooks.
  struct Config {
    Config();
    Config(const Config&);
    Config& operator=(const Config&);
    ~Config();

    GURL releases_url;
    base::FilePath downloads;
    bool opens_download = true;

    static Config FromEnvironment();
  };

  // `quit` quits Pane, as Quit and install does once the installer is open.
  Updater(std::string current_version,
          Config config,
          scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
          base::RepeatingClosure quit);
  Updater(const Updater&) = delete;
  Updater& operator=(const Updater&) = delete;
  ~Updater();

  Status status() const { return status_; }
  // The newest release, while one is on offer.
  const std::optional<model::AvailableUpdate>& available() const {
    return available_;
  }
  // 0 to 1 while a download runs, and its whole seconds left once the rate
  // settles.
  std::optional<double> download_progress() const { return download_progress_; }
  std::optional<int> download_eta() const { return download_eta_; }
  // A check completed this launch.
  bool checked() const { return last_checked_at_.has_value(); }
  // The new version waits beside the app, to install at quit (K1).
  bool staged() const { return false; }
  // The status line the pill and the Lock show.
  std::u16string Label() const;
  // The pill shows while an update is on offer and not dismissed.
  bool ShowsPill() const;

  // Checks now, then six hours after each completed check, or 5, 15, then 60
  // minutes after a failed one.
  void Schedule();
  // Settings' Check for updates, the app menu's, and the schedule's. An
  // offer stays up while its check runs, so the pill never blinks, and a
  // download under way runs on. `done` hears whether it completed.
  void Check(base::OnceCallback<void(bool completed)> done = {});
  // The pill's dismiss: hidden for this version until relaunch.
  void DismissPill();
  // The pill's click and Settings' Download update: downloads the dmg; once
  // downloaded, opens it and quits for the user to install (Quit and
  // install). A failure returns to available, so the user can try again
  // from the same place.
  void DownloadUpdate();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  // The answer to a check, once the checking state has held long enough.
  void Checked(base::OnceCallback<void(bool)> done,
               base::TimeTicks started,
               std::optional<std::string> body);
  void Settle(base::OnceCallback<void(bool)> done,
              bool completed,
              std::optional<model::AvailableUpdate> update);
  void Scheduled(bool completed);
  // What a completed check found: a download under way runs on, and the
  // version already downloaded stays downloaded.
  void Offer(std::optional<model::AvailableUpdate> update);

  void StartDownload(model::AvailableUpdate update);
  void DownloadStarted(int64_t total);
  void DownloadProgressed(uint64_t received);
  void Downloaded(model::AvailableUpdate update, base::FilePath file);
  void Verified(std::string version, std::optional<base::FilePath> file);
  // The downloaded dmg is still where it was saved: open it and quit.
  void QuitAndInstall(bool exists);
  void Open(const base::FilePath& file);

  void SetStatus(Status status);
  void Notify();

  std::unique_ptr<network::ResourceRequest> RequestFor(const GURL& url) const;

  const std::string current_version_;
  const Config config_;
  const scoped_refptr<network::SharedURLLoaderFactory> loader_factory_;
  const base::RepeatingClosure quit_;

  Status status_ = Status::kIdle;
  std::optional<model::AvailableUpdate> available_;
  std::optional<base::Time> last_checked_at_;
  std::optional<double> download_progress_;
  std::optional<int> download_eta_;
  std::optional<std::string> dismissed_version_;
  // The dmg downloaded this launch, which the pill opens again.
  std::optional<std::string> downloaded_version_;
  base::FilePath downloaded_file_;

  std::unique_ptr<network::SimpleURLLoader> check_loader_;
  bool checking_ = false;
  base::OneShotTimer schedule_timer_;
  size_t retry_ = 0;

  std::unique_ptr<network::SimpleURLLoader> download_loader_;
  int64_t download_total_ = -1;
  std::optional<model::DownloadETA> eta_;

  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<Updater> weak_factory_{this};
};

// The status's name, as the channel and the Swift's snapshot give it.
std::string_view UpdateStatusName(Updater::Status status);

}  // namespace pane

#endif  // PANE_UPDATES_UPDATER_H_
