// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UPDATES_UPDATER_H_
#define PANE_UPDATES_UPDATER_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "pane/model/updates.h"
#include "pane/updates/update_installer.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
struct ResourceRequest;
}  // namespace network

namespace pane {

class DmgDownload;

// The update check, download, and install (the Swift's Updater, ADR 0002 and
// 0092; DESIGN.md › Updates; CONTEXT.md: update check): the repo's latest
// GitHub Release at launch and every six hours, with retries after a failure;
// a click downloads its dmg to Downloads (DmgDownload), and once it checks
// out against the release's SHA-256 it leaves quarantine and Pane stages the
// new version beside itself (UpdateInstaller). Restart to update quits into
// it, and any quit installs it, until a check finds a newer one, which
// replaces it, or names an older latest release, a release withdrawn, which
// takes it back. Where Pane can't replace itself, the dmg opens and the user
// drags Pane in (Quit and install). Never a modal, never a forced restart.
// The pill, Settings, and the Lock read this one source.
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

  // Where it looks and saves, whether it opens what it saved, and whether it
  // installs it: GitHub's latest release, the user's Downloads, and yes,
  // unless a scenario's copy (the mock keychain) points them elsewhere
  // (PANE_UPDATE_URL, PANE_UPDATE_DOWNLOADS, PANE_UPDATE_OPEN=0,
  // PANE_UPDATE_INSTALL=0; test_hooks.h).
  struct Config {
    Config();
    Config(const Config&);
    Config& operator=(const Config&);
    ~Config();

    GURL releases_url;
    base::FilePath downloads;
    bool opens_download = true;
    bool installs = true;
    // How Restart to update opens the new Pane.
    UpdateInstaller::Relaunch relaunch;

    static Config FromEnvironment();
  };

  // `quit` quits Pane: Quit and install once the installer is open, and
  // Restart to update.
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
  // A check completed this launch, and when the last one did.
  bool checked() const { return last_checked_at_.has_value(); }
  std::optional<base::Time> last_checked_at() const { return last_checked_at_; }
  // The new version waits beside the app, to install at quit.
  bool staged() const { return !staged_.empty(); }
  // The last exit's swap failed and rolled back, the staged version kept for
  // the next: the line above the pill and Settings' row say so, for this
  // launch, until the pill is clicked or dismissed (Paper 11.12.14, E).
  bool install_failed() const { return install_failed_ && staged(); }
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
  // The pill's click, Settings' button, and the Lock's action: downloads the
  // dmg; once downloaded, restarts into it, or, where Pane can't install it
  // itself, opens it and quits for the user to (Quit and install). A failure
  // returns to available, so the user can try again from the same place.
  void DownloadUpdate();
  // At every quit, a staged update swaps in. A swap macOS refuses leaves the
  // app as it was, and the dmg opens instead. A staged copy a check took back
  // goes first, if its removal hasn't begun.
  void InstallAtQuit();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  // A staged copy on its way out (Unstaging, updater.cc).
  class Unstaging;

  // The answer to a check, once the checking state has held long enough.
  void Checked(base::OnceCallback<void(bool)> done,
               base::TimeTicks started,
               std::optional<std::string> body);
  void Settle(base::OnceCallback<void(bool)> done,
              bool completed,
              std::optional<model::AvailableUpdate> update,
              std::optional<std::string> latest);
  void Scheduled(bool completed);
  // What the last exit left staged, offered again (Windows').
  void KeptFromLastExit(std::optional<UpdateInstaller::Kept> kept);
  // What a completed check found: a download under way runs on, and so does
  // a quit for Restart to update; the version already downloaded stays
  // downloaded, unless a newer one replaces what was staged, or an older
  // latest takes it back.
  void Offer(std::optional<model::AvailableUpdate> update);
  // Whether the last completed check took back `version`, staged: its latest
  // release is older (ADR 0092, a release withdrawn after it staged).
  bool Withdrawn(const std::string& version) const;
  // The staged copy and its dmg, which a newer offer passed or a check took
  // back: they go, so no quit installs them.
  void Unstage();

  void StartDownload(model::AvailableUpdate update);
  void DownloadProgressed(double share, std::optional<int> seconds);
  // What was pending of the progress, at most once a frame.
  void ProgressSettled();
  void Downloaded(model::AvailableUpdate update,
                  std::optional<base::FilePath> file);
  void Verified(std::string version,
                bool verified,
                std::optional<base::FilePath> file);
  // The checked dmg's Pane, copied beside this one or not.
  void Staged(base::FilePath file, std::optional<base::FilePath> staged);
  // The downloaded dmg is still where it was saved: open it and quit.
  void QuitAndInstall(bool exists);
  void Open(const base::FilePath& file);
  // As Open, once Pane has exited: what opens as Pane quits
  // (OpenUpdateFileAfterExit).
  void OpenAfterExit(const base::FilePath& file);

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
  // The latest release's version, as the last completed check read it,
  // whatever file it carries.
  std::optional<std::string> latest_version_;
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

  std::unique_ptr<DmgDownload> download_;
  // The download's progress, told at most once a frame: a fast download
  // brings more pieces than the screen shows.
  base::OneShotTimer progress_timer_;
  bool progress_pending_ = false;

  // Nullopt where Pane can't replace itself, and in scenarios that turn it
  // off. Its work runs on a sequence of its own, an earlier install's
  // leftovers removed before anything is staged.
  const std::optional<UpdateInstaller> installer_;
  const scoped_refptr<base::SequencedTaskRunner> install_runner_;
  // The downloaded version's Pane, staged beside this one to swap in at quit.
  base::FilePath staged_;
  // Staged copies on their way out, whose removal a quit runs itself if the
  // installer's sequence hasn't begun it.
  std::vector<scoped_refptr<Unstaging>> unstaging_;
  // Restart to update: the quit opens the new Pane too.
  bool relaunches_ = false;
  // The last exit's swap failed (install_failed()).
  bool install_failed_ = false;

  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<Updater> weak_factory_{this};
};

// The status's name, as the channel gives it.
std::string_view UpdateStatusName(Updater::Status status);

}  // namespace pane

#endif  // PANE_UPDATES_UPDATER_H_
