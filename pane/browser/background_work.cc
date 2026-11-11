// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/background_work.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool/thread_pool_instance.h"

namespace pane {

namespace {

// A wait's answer, given once, on the sequence that asked: by the pool's
// flush, or by the cap, whichever comes first.
class Wait : public base::RefCountedThreadSafe<Wait> {
 public:
  explicit Wait(base::OnceCallback<void(bool)> done) : done_(std::move(done)) {}

  void Answer(bool ran) {
    if (done_) {
      std::move(done_).Run(ran);
    }
  }

 private:
  friend class base::RefCountedThreadSafe<Wait>;
  ~Wait() = default;

  base::OnceCallback<void(bool)> done_;
};

}  // namespace

void WaitForBackgroundWork(base::TimeDelta cap,
                           base::OnceCallback<void(bool)> done) {
  scoped_refptr<base::SequencedTaskRunner> here =
      base::SequencedTaskRunner::GetCurrentDefault();
  auto wait = base::MakeRefCounted<Wait>(std::move(done));
  here->PostDelayedTask(FROM_HERE, base::BindOnce(&Wait::Answer, wait, false),
                        cap);
  // Answered from the pool's own threads once nothing is left in it, or at
  // its shutdown; Chromium keeps every flush asked for meanwhile.
  base::ThreadPoolInstance::Get()->FlushAsyncForTesting(
      base::BindPostTask(here, base::BindOnce(&Wait::Answer, wait, true)));
}

}  // namespace pane
