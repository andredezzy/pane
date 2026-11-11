// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/dmg_download.h"

#include <algorithm>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "net/base/net_errors.h"
#include "net/http/http_byte_range.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace pane {

namespace {

constexpr net::NetworkTrafficAnnotationTag kUpdateDownload =
    net::DefineNetworkTrafficAnnotation("pane_update_download", R"(
      semantics {
        sender: "Pane's update download"
        description:
          "Downloads the offered release's dmg, on a new connection when one "
          "crawls, for the rest from where it got to where the server takes "
          "ranges."
        trigger: "The user's click on the update pill, in Settings, or at "
          "the Lock."
        data: "None: a plain GET with Pane's user agent, and a range."
        destination: OTHER
        destination_other: "github.com"
      }
      policy {
        cookies_allowed: NO
        setting: "None."
        policy_exception_justification: "Pane has no enterprise policy."
      })");

}  // namespace

// The file the bytes go into, on a sequence that may block: a temporary file,
// deleted unless it is handed over whole.
class DmgDownload::File {
 public:
  File() {
    if (base::CreateTemporaryFile(&path_)) {
      file_.Initialize(path_, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
    }
  }
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  ~File() {
    file_.Close();
    if (!path_.empty()) {
      base::DeleteFile(path_);
    }
  }

  bool Append(std::string data) {
    written_ = written_ && file_.IsValid() &&
               file_.WriteAtCurrentPosAndCheck(base::as_byte_span(data));
    return written_;
  }

  // From `length` on: the rest goes after what it already holds, or the
  // whole file from the start.
  bool TruncateTo(int64_t length) {
    written_ = written_ && file_.IsValid() && file_.SetLength(length) &&
               file_.Seek(base::File::FROM_BEGIN, length) == length;
    return written_;
  }

  // Handed over, closed, the caller owning it from here; never once a write
  // failed, and then it goes with this.
  std::optional<base::FilePath> Close() {
    if (!file_.IsValid() || !written_) {
      return std::nullopt;
    }
    file_.Close();
    return std::exchange(path_, base::FilePath());
  }

 private:
  base::FilePath path_;
  base::File file_;
  bool written_ = true;
};

DmgDownload::DmgDownload(
    const network::ResourceRequest& request,
    scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
    Progress progress,
    Done done)
    : request_(request),
      loader_factory_(std::move(loader_factory)),
      progress_(std::move(progress)),
      done_(std::move(done)),
      // The user's own request, its progress in view: never left to
      // best-effort tasks, which Chromium holds until startup completes.
      file_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})),
      pace_(base::Time::Now()) {
  Start(/*resuming=*/false);
}

DmgDownload::~DmgDownload() = default;

// On a request of its own: the last one's connection, which crawled, is let
// go with it.
void DmgDownload::Start(bool resuming) {
  ++connection_;
  loader_.reset();
  judge_timer_.Stop();
  retry_timer_.Stop();

  auto request = std::make_unique<network::ResourceRequest>(request_);
  resuming_ = resuming && received_ > 0;
  if (resuming_) {
    asked_ = received_;
    request->headers.SetHeader(
        net::HttpRequestHeaders::kRange,
        net::HttpByteRange::RightUnbounded(asked_).GetHeaderValue());
    // A file that changed meanwhile comes whole, not spliced.
    if (!validator_.empty()) {
      request->headers.SetHeader(net::HttpRequestHeaders::kIfRange, validator_);
    }
    // The new connection's rate says nothing of the last one's.
    eta_.RestartRate();
  } else {
    asked_ = 0;
    received_ = 0;
    total_ = -1;
    eta_ = model::DownloadETA();
    file_.AsyncCall(&File::TruncateTo)
        .WithArgs(0)
        .Then(base::BindOnce(&DmgDownload::Wrote, weak_factory_.GetWeakPtr(),
                             base::DoNothing()));
  }
  answer_ = model::DownloadAnswer();
  has_bytes_ = false;
  pace_.Begin(received_, base::Time::Now());

  loader_ =
      network::SimpleURLLoader::Create(std::move(request), kUpdateDownload);
  loader_->SetOnResponseStartedCallback(base::BindOnce(
      [](base::WeakPtr<DmgDownload> download, int connection, const GURL&,
         const network::mojom::URLResponseHead& head) {
        if (download) {
          download->Answered(connection, head);
        }
      },
      weak_factory_.GetWeakPtr(), connection_));
  loader_->DownloadAsStream(loader_factory_.get(), this);
  judge_timer_.Start(
      FROM_HERE, pace_.window(),
      base::BindOnce(&DmgDownload::Judge, weak_factory_.GetWeakPtr()));
}

void DmgDownload::Answered(int connection,
                           const network::mojom::URLResponseHead& head) {
  if (connection != connection_ || !head.headers) {
    return;
  }
  const net::HttpResponseHeaders& headers = *head.headers;
  answer_ = {
      .status = headers.response_code(),
      .offers_ranges = headers.response_code() == 206 ||
                       headers.HasHeaderValue("Accept-Ranges", "bytes"),
      .validated = headers.HasValidators(),
  };
  // Anything but the file is SimpleURLLoader's to end (OnComplete).
  if (answer_.status != 200 && answer_.status != 206) {
    return;
  }

  if (resuming_) {
    int64_t first = -1;
    int64_t last = -1;
    int64_t length = -1;
    const bool ranged = headers.GetContentRangeFor206(&first, &last, &length);
    switch (model::ReadRangeAnswer(
        answer_.status, ranged ? std::optional(first) : std::nullopt, asked_)) {
      case model::RangeAnswer::kRest:
        // Under way again: its window runs from here, as from first bytes.
        total_ = length;
        has_bytes_ = true;
        pace_.Begin(received_, base::Time::Now());
        break;
      case model::RangeAnswer::kWhole:
        // The whole file again: from the start, with this answer.
        resuming_ = false;
        received_ = 0;
        total_ = head.content_length;
        eta_ = model::DownloadETA();
        file_.AsyncCall(&File::TruncateTo)
            .WithArgs(0)
            .Then(base::BindOnce(&DmgDownload::Wrote,
                                 weak_factory_.GetWeakPtr(),
                                 base::DoNothing()));
        break;
      case model::RangeAnswer::kRefused:
        StartOver();
        return;
    }
  } else {
    total_ = head.content_length;
  }

  last_answer_ = answer_;
  validator_.clear();
  if (std::optional<std::string> etag = headers.GetNormalizedHeader("ETag")) {
    validator_ = std::move(*etag);
  } else if (std::optional<std::string> modified =
                 headers.GetNormalizedHeader("Last-Modified")) {
    validator_ = std::move(*modified);
  }
}

void DmgDownload::OnDataReceived(std::string_view data,
                                 base::OnceClosure resume) {
  const base::Time now = base::Time::Now();
  // The connection's setup (DNS, TLS, a redirect) says nothing of its rate:
  // its window runs from its first bytes.
  if (!has_bytes_) {
    has_bytes_ = true;
    pace_.Begin(received_, now);
  }
  // Written in order, after anything asked of the file before; the next
  // bytes are read once these are.
  file_.AsyncCall(&File::Append)
      .WithArgs(std::string(data))
      .Then(base::BindOnce(&DmgDownload::Wrote, weak_factory_.GetWeakPtr(),
                           std::move(resume)));
  received_ += static_cast<int64_t>(data.size());
  drops_ = 0;

  if (total_ > 0) {
    progress_.Run(std::min(static_cast<double>(received_) / total_, 1.0),
                  eta_.Update(received_, total_, now));
  }
}

void DmgDownload::Wrote(base::OnceClosure resume, bool written) {
  if (!written) {
    LOG(ERROR) << "The update's download could not be written";
    Fail();
    return;
  }
  // A connection left meanwhile ignores it.
  std::move(resume).Run();
}

void DmgDownload::OnComplete(bool success) {
  if (finished_) {
    return;
  }
  const int error = loader_ ? loader_->NetError() : net::ERR_FAILED;
  loader_.reset();
  judge_timer_.Stop();

  // 206 is a resumed download's last part, in a file whole by now.
  const bool brought_file = answer_.status == 200 || answer_.status == 206;
  if (success && brought_file) {
    // The file whole, once every write before is done.
    finished_ = true;
    file_.AsyncCall(&File::Close)
        .Then(
            base::BindOnce(&DmgDownload::Finished, weak_factory_.GetWeakPtr()));
    return;
  }

  // The server answered with something other than the file: a range it
  // refused is asked for no more, and the file comes from the start, once.
  if (success || (answer_.status != 0 && !brought_file)) {
    if (resuming_ && !refused_) {
      StartOver();
      return;
    }
    LOG(ERROR) << "The update's download was answered " << answer_.status;
    Fail();
    return;
  }

  // Dropped: on from where it got to, in a moment.
  if (CanGoOn()) {
    if (const std::optional<base::TimeDelta> delay =
            model::DropRetryDelay(drops_)) {
      ++drops_;
      LOG(WARNING) << "The update's download dropped (" << error << ") "
                   << received_ << " bytes in, and goes on";
      retry_timer_.Start(
          FROM_HERE, *delay,
          base::BindOnce(&DmgDownload::Start, weak_factory_.GetWeakPtr(),
                         /*resuming=*/true));
      return;
    }
  }

  // A resumed connection ended with no answer: refused rather than dropped.
  if (resuming_ && drops_ == 0 && !refused_) {
    StartOver();
    return;
  }

  LOG(ERROR) << "The update's download failed: " << error;
  Fail();
}

void DmgDownload::OnRetry(base::OnceClosure start_retry) {
  // SimpleURLLoader retries nothing here: a new connection is this class's.
  NOTREACHED();
}

void DmgDownload::Judge() {
  if (finished_ || !loader_) {
    return;
  }
  const base::Time now = base::Time::Now();
  // Its first bytes came since this was set, and the window runs from them.
  const base::TimeDelta left = pace_.TimeLeft(now);
  if (left.is_positive()) {
    judge_timer_.Start(
        FROM_HERE, left,
        base::BindOnce(&DmgDownload::Judge, weak_factory_.GetWeakPtr()));
    return;
  }
  const bool can_leave = model::CanLeaveConnection(resuming_, answer_, refused_,
                                                   received_, total_);
  if (!pace_.ShouldSwitch(received_, now, can_leave)) {
    judge_timer_.Start(
        FROM_HERE, pace_.window(),
        base::BindOnce(&DmgDownload::Judge, weak_factory_.GetWeakPtr()));
    return;
  }
  LOG(WARNING) << "The update's download left a slow connection " << received_
               << " bytes in";
  // A resumed one cancelled before its answer gives none: where the last one
  // stopped still stands.
  Start(/*resuming=*/CanGoOn());
}

void DmgDownload::StartOver() {
  LOG(WARNING) << "The update's download could not resume, and starts over";
  refused_ = true;
  Start(/*resuming=*/false);
}

bool DmgDownload::CanGoOn() const {
  return received_ > 0 && model::CanResume(last_answer_, refused_);
}

void DmgDownload::Finished(std::optional<base::FilePath> file) {
  if (done_) {
    std::move(done_).Run(std::move(file));
  }
}

void DmgDownload::Fail() {
  finished_ = true;
  loader_.reset();
  judge_timer_.Stop();
  retry_timer_.Stop();
  // The file goes with its sequence's object.
  file_.Reset();
  if (done_) {
    std::move(done_).Run(std::nullopt);
  }
}

}  // namespace pane
