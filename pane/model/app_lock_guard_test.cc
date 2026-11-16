// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/app_lock_guard_test.h"

#include <utility>

namespace pane::model {

GuardTestVaultDelegate::GuardTestVaultDelegate() = default;
GuardTestVaultDelegate::~GuardTestVaultDelegate() = default;

void GuardTestVaultDelegate::DataKeyDidOpen(base::OnceClosure loaded) {
  std::move(loaded).Run();
}

std::vector<std::string> GuardTestVaultDelegate::ProfileIds() {
  return {};
}

void GuardTestVaultDelegate::AccessDidOpen() {}
void GuardTestVaultDelegate::VolumeWontOpen() {}
void GuardTestVaultDelegate::QuitToCloseVolume() {}
void GuardTestVaultDelegate::VolumeDidDetachAtLock() {}

GuardTestLockDelegate::GuardTestLockDelegate() = default;
GuardTestLockDelegate::~GuardTestLockDelegate() = default;

void GuardTestLockDelegate::DidUnlock() {
  ++unlocks;
}

void GuardTestLockDelegate::DidAskToLock() {
  ++asked_to_lock;
  if (lock_now) {
    lock_now.Run();
  }
}

GuardTestShakes::GuardTestShakes() = default;
GuardTestShakes::~GuardTestShakes() = default;

void GuardTestShakes::OnPinShake() {
  ++count;
}

AppLockGuardTest::AppLockGuardTest() = default;
AppLockGuardTest::~AppLockGuardTest() = default;

void AppLockGuardTest::SetUp() {
  ASSERT_TRUE(directory_.CreateUniqueTempDir());
  data_ = directory_.GetPath().AppendASCII("data");
  ASSERT_TRUE(base::CreateDirectory(data_));
  guard_ = base::MakeRefCounted<StandInPinGuard>();
  lock_delegate_.lock_now = base::BindLambdaForTesting([&] {
    lock_->LockNow();
    if (lock_->is_locked()) {
      vault_->Lock().Run(/*every_closed=*/true);
    }
  });
}

void AppLockGuardTest::TearDown() {
  task_environment_.RunUntilIdle();
  if (lock_) {
    lock_->RemoveObserver(&shakes_);
  }
  lock_.reset();
  vault_.reset();
}

}  // namespace pane::model
