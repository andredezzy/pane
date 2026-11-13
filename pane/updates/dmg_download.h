// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UPDATES_DMG_DOWNLOAD_H_
#define PANE_UPDATES_DMG_DOWNLOAD_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "base/timer/timer.h"
#include "pane/model/updates.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
namespace mojom {
class URLResponseHead;
}  // namespace mojom
}  // namespace network

namespace pane {

// One dmg download (the Swift's DmgDownload, ADR 0002), its bytes in a
// temporary file, with its progress and time left. A connection that crawls
// is left for another (model::DownloadPace). Where the server takes ranges
// and names the file's version, the rest comes from where it got to, the
// progress never going back; where it offers none, or refuses them when
// asked, the file comes from the start, and a server that refused is never
// asked again. A connection that drops goes on from where it got to, after 2,
// 4, then 8 s in a row with nothing brought. SimpleURLLoader keeps no resume
// data, so each connection is a request of its own, the rest asked for by
// range.
class DmgDownload : public network::SimpleURLLoaderStreamConsumer {
 public:
  // The share downloaded, 0 to 1, and the whole seconds left once the rate
  // settles. Unheard while the file's length is unknown.
  using Progress =
      base::RepeatingCallback<void(double share, std::optional<int> seconds)>;
  // The whole file, in a temporary file the caller then owns, or nullopt
  // when the download failed, its file gone.
  using Done = base::OnceCallback<void(std::optional<base::FilePath> file)>;

  // Starts at once. `request` is each connection's, the rest asked for by
  // range.
  DmgDownload(const network::ResourceRequest& request,
              scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
              Progress progress,
              Done done);
  DmgDownload(const DmgDownload&) = delete;
  DmgDownload& operator=(const DmgDownload&) = delete;
  ~DmgDownload() override;

  // network::SimpleURLLoaderStreamConsumer:
  void OnDataReceived(std::string_view data, base::OnceClosure resume) override;
  void OnComplete(bool success) override;
  void OnRetry(base::OnceClosure start_retry) override;

 private:
  class File;

  // A new connection, for the rest from where the file got to when
  // `resuming`, or for the whole file.
  void Start(bool resuming);
  void Answered(int connection, const network::mojom::URLResponseHead& head);
  void Wrote(base::OnceClosure resume, bool written);
  // At the end of the connection's window: kept, or left for another.
  void Judge();
  // The server refused to go on from where it got to (GitHub's signed link
  // expires): from the start, once.
  void StartOver();
  // Whether the rest can come from where the file got to.
  bool CanGoOn() const;
  void Finished(std::optional<base::FilePath> file);
  void Fail();

  const network::ResourceRequest request_;
  const scoped_refptr<network::SharedURLLoaderFactory> loader_factory_;
  const Progress progress_;
  Done done_;

  base::SequenceBound<File> file_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  // Counts the connections, so a write's answer knows its own.
  int connection_ = 0;

  model::DownloadPace pace_;
  model::DownloadETA eta_;
  // What the file holds, and its whole length (below 0 while unknown).
  int64_t received_ = 0;
  int64_t total_ = -1;
  // The connection asked for the rest from `asked_`.
  bool resuming_ = false;
  int64_t asked_ = 0;
  // What this connection answered, and the last answer that brought the
  // file, whose validator a range request sends back.
  model::DownloadAnswer answer_;
  model::DownloadAnswer last_answer_;
  std::string validator_;
  // The connection has brought something, so its window runs from there.
  bool has_bytes_ = false;
  // The server refused to go on from where a connection left off: it is not
  // asked again.
  bool refused_ = false;
  // Connections dropped in a row with nothing brought since.
  int drops_ = 0;
  bool finished_ = false;

  base::OneShotTimer judge_timer_;
  base::OneShotTimer retry_timer_;
  base::WeakPtrFactory<DmgDownload> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UPDATES_DMG_DOWNLOAD_H_
