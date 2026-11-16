// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/unpack.h"

#include <windows.h>

#include <bcrypt.h>
#include <psapi.h>

#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/memory_mapped_file.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/raw_span.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/types/expected_macros.h"
#include "chrome/installer/util/unbuffered_file_writer.h"
#include "third_party/lzma_sdk/google/seven_zip_reader.h"

namespace pane::installer {

namespace {

// Errors Windows gives where an antivirus or Windows Security stopped a file.
bool IsBlock(DWORD error) {
  return error == ERROR_VIRUS_INFECTED || error == ERROR_VIRUS_DELETED ||
         error == ERROR_ACCESS_DENIED;
}

// The delegate lzma_util.cc's SevenZipDelegateImpl is (UnPackArchive's), the
// same steps for each file, with the bytes it wrote counted and a stop
// between files.
class Delegate : public seven_zip::Delegate {
 public:
  Delegate(const base::FilePath& folder,
           UnpackProgress* progress,
           const std::atomic<bool>& cancel)
      : folder_(folder), progress_(progress), cancel_(cancel) {}

  UnpackResult Result() const {
    if (failed_) {
      return result_;
    }
    if (cancel_->load()) {
      return {.outcome = UnpackResult::Outcome::kCancelled};
    }
    return {.outcome = UnpackResult::Outcome::kDone};
  }

  // seven_zip::Delegate:
  void OnOpenError(seven_zip::Result result) override {
    Fail(base::FilePath(),
         ::GetLastError() ? ::GetLastError() : ERROR_FILE_CORRUPT);
  }

  // Where a folder of the archive (in 7z's sense) is unpacked whole before
  // its files are copied out: beside them, as UnPackArchive keeps it, gone
  // once closed.
  base::File OnTempFileRequest() override {
    base::FilePath path;
    base::File file;
    if (base::CreateTemporaryFileInDir(folder_, &path)) {
      file.Initialize(path, base::File::FLAG_CREATE_ALWAYS |
                                base::File::FLAG_READ | base::File::FLAG_WRITE |
                                base::File::FLAG_WIN_EXCLUSIVE_READ |
                                base::File::FLAG_WIN_EXCLUSIVE_WRITE |
                                base::File::FLAG_WIN_TEMPORARY |
                                base::File::FLAG_DELETE_ON_CLOSE |
                                base::File::FLAG_WIN_SHARE_DELETE);
    }
    if (!file.IsValid()) {
      Fail(base::FilePath(), ::GetLastError());
    }
    return file;
  }

  bool OnEntry(const seven_zip::EntryInfo& entry,
               base::span<uint8_t>& output) override {
    if (cancel_->load()) {
      return false;
    }
    if (entry.file_path.ReferencesParent() || entry.file_path.IsAbsolute()) {
      Fail(entry.file_path, ERROR_INVALID_NAME);
      return false;
    }
    const base::FilePath path = folder_.Append(entry.file_path);
    MakeFolder(path.DirName());
    ASSIGN_OR_RETURN(
        current_,
        ::installer::UnbufferedFileWriter::Create(path, entry.file_size),
        [&](DWORD error) {
          Fail(entry.file_path, error);
          return false;
        });
    output = current_->write_buffer().first(entry.file_size);
    ::SetLastError(ERROR_SUCCESS);
    return true;
  }

  bool OnDirectory(const seven_zip::EntryInfo& entry) override {
    if (entry.file_path.ReferencesParent() || entry.file_path.IsAbsolute() ||
        !MakeFolder(folder_.Append(entry.file_path))) {
      Fail(entry.file_path, ::GetLastError());
      return false;
    }
    return true;
  }

  bool EntryDone(seven_zip::Result result,
                 const seven_zip::EntryInfo& entry) override {
    ::installer::UnbufferedFileWriter file = *std::move(current_);
    current_.reset();
    if (result != seven_zip::Result::kSuccess) {
      const DWORD error = ::GetLastError();
      Fail(entry.file_path, result == seven_zip::Result::kDiskFull
                                ? ERROR_DISK_FULL
                            : error != ERROR_SUCCESS ? error
                                                     : ERROR_FILE_CORRUPT);
      return false;
    }
    file.Advance(entry.file_size);
    RETURN_IF_ERROR(
        file.Commit(entry.last_modified_time.is_null()
                        ? std::nullopt
                        : std::optional<base::Time>(entry.last_modified_time)),
        [&](DWORD error) {
          Fail(entry.file_path, error);
          return false;
        });
    if (progress_) {
      progress_->written += entry.file_size;
    }
    return !cancel_->load();
  }

 private:
  bool MakeFolder(const base::FilePath& path) {
    if (folders_.contains(path)) {
      return true;
    }
    if (!base::CreateDirectory(path)) {
      return false;
    }
    folders_.insert(path);
    return true;
  }

  void Fail(const base::FilePath& file, DWORD error) {
    if (failed_) {
      return;
    }
    failed_ = true;
    result_ = {.outcome = IsBlock(error) ? UnpackResult::Outcome::kBlocked
                                         : UnpackResult::Outcome::kFailed,
               .file = file,
               .error = error ? error : ERROR_GEN_FAILURE};
    LOG(ERROR) << "Unpacking stopped at " << file << ": " << error;
  }

  const base::FilePath folder_;
  const raw_ptr<UnpackProgress> progress_;
  const raw_ref<const std::atomic<bool>> cancel_;
  std::set<base::FilePath> folders_;
  std::optional<::installer::UnbufferedFileWriter> current_;
  bool failed_ = false;
  UnpackResult result_;
};

// How much of the archive the reader has read: its pages, mapped from the
// installer, that the reader has touched, which Windows maps into this
// process's working set one by one as it reads them (QueryWorkingSetEx). A
// file's position would jump between a BCJ2 folder's four streams; the pages
// read only add up.
class ReadWatch : public base::PlatformThread::Delegate {
 public:
  ReadWatch(base::span<const uint8_t> archive, std::atomic<uint64_t>& read)
      : archive_(archive), read_(read) {
    const uintptr_t start = reinterpret_cast<uintptr_t>(archive.data());
    const uintptr_t first = start & ~(kPage - 1);
    const uintptr_t end = start + archive.size();
    for (uintptr_t page = first; page < end; page += kPage) {
      PSAPI_WORKING_SET_EX_INFORMATION info = {};
      info.VirtualAddress = reinterpret_cast<void*>(page);
      pages_.push_back(info);
    }
  }

  void Start() { base::PlatformThread::Create(0, this, &thread_); }

  void Stop() {
    stop_.Signal();
    if (!thread_.is_null()) {
      base::PlatformThread::Join(thread_);
    }
  }

  // base::PlatformThread::Delegate:
  void ThreadMain() override {
    base::PlatformThread::SetName("PaneReadWatch");
    do {
      if (::QueryWorkingSetEx(
              ::GetCurrentProcess(), pages_.data(),
              static_cast<DWORD>(pages_.size() *
                                 sizeof(PSAPI_WORKING_SET_EX_INFORMATION)))) {
        uint64_t valid = 0;
        for (const PSAPI_WORKING_SET_EX_INFORMATION& page : pages_) {
          valid += page.VirtualAttributes.Valid;
        }
        const uint64_t read =
            std::min<uint64_t>(valid * kPage, archive_.size());
        if (read > read_->load()) {
          read_->store(read);
        }
      }
    } while (!stop_.TimedWait(base::Milliseconds(15)));
  }

 private:
  static constexpr uintptr_t kPage = 4096;

  const base::raw_span<const uint8_t> archive_;
  const raw_ref<std::atomic<uint64_t>> read_;
  std::vector<PSAPI_WORKING_SET_EX_INFORMATION> pages_;
  base::WaitableEvent stop_;
  base::PlatformThreadHandle thread_;
};

}  // namespace

UnpackResult Unpack(const base::FilePath& installer,
                    const Payload& payload,
                    const base::FilePath& folder,
                    UnpackProgress* progress,
                    const std::atomic<bool>& cancel) {
  // The archive, mapped from the installer as UnPackArchiveBuffer takes one.
  base::MemoryMappedFile mapped;
  base::File file(installer, base::File::FLAG_OPEN | base::File::FLAG_READ |
                                 base::File::FLAG_WIN_SHARE_DELETE);
  if (!file.IsValid() ||
      !mapped.Initialize(std::move(file),
                         {static_cast<int64_t>(payload.offset),
                          static_cast<size_t>(payload.size)},
                         base::MemoryMappedFile::READ_ONLY)) {
    return {.outcome = UnpackResult::Outcome::kFailed,
            .error = ::GetLastError() ? ::GetLastError() : ERROR_READ_FAULT};
  }
  std::optional<ReadWatch> watch;
  if (progress) {
    watch.emplace(mapped.bytes(), progress->read);
    watch->Start();
  }
  Delegate delegate(folder, progress, cancel);
  if (std::unique_ptr<seven_zip::SevenZipReader> reader =
          seven_zip::SevenZipReader::Create(mapped.bytes(), delegate)) {
    reader->Extract();
  }
  if (watch) {
    watch->Stop();
  }
  UnpackResult result = delegate.Result();
  if (progress && result.outcome == UnpackResult::Outcome::kDone) {
    progress->read = payload.size;
  }
  return result;
}

bool PayloadIsIntact(const base::FilePath& installer, const Payload& payload) {
  base::File file(installer, base::File::FLAG_OPEN | base::File::FLAG_READ |
                                 base::File::FLAG_WIN_SHARE_DELETE |
                                 base::File::FLAG_WIN_SEQUENTIAL_SCAN);
  if (!file.IsValid()) {
    return false;
  }
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (!BCRYPT_SUCCESS(::BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash,
                                         nullptr, 0, nullptr, 0, 0))) {
    return false;
  }
  std::vector<uint8_t> buffer(1 << 20);
  uint64_t offset = payload.offset;
  const uint64_t end = payload.offset + payload.size;
  bool read_all = true;
  while (offset < end) {
    const size_t count =
        static_cast<size_t>(std::min<uint64_t>(buffer.size(), end - offset));
    const std::optional<size_t> read = file.Read(
        static_cast<int64_t>(offset), base::span(buffer).first(count));
    if (!read || *read == 0) {
      read_all = false;
      break;
    }
    ::BCryptHashData(hash, buffer.data(), static_cast<ULONG>(*read), 0);
    offset += *read;
  }
  std::array<uint8_t, 32> digest = {};
  const bool hashed =
      BCRYPT_SUCCESS(::BCryptFinishHash(hash, digest.data(), digest.size(), 0));
  ::BCryptDestroyHash(hash);
  return read_all && hashed && digest == payload.sha256;
}

}  // namespace pane::installer
