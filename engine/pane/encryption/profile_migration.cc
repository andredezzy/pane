// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/profile_migration.h"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/numerics/byte_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/system/sys_info.h"
#include "base/time/time.h"
#include "net/disk_cache/cache_entry_hasher.h"
#include "net/disk_cache/disk_cache.h"
#include "net/disk_cache/pane_cache_hook.h"
#include "pane/encryption/engine_keys.h"
#include "pane/encryption/sqlite_vfs.h"
#include "pane/encryption/stream_cipher.h"
#include "pane/encryption/whole_file.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "storage/browser/file_system/pane_encrypted_stream.h"
#include "storage/common/database/database_identifier.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace pane {

namespace {

using encryption::PathKind;
using encryption::PathVerdict;
using encryption::ProfileKeys;
using encryption::StreamCipher;

constexpr std::string_view kTemporarySuffix = ".pane-migrating";
constexpr size_t kBufferSize = 1 << 20;
constexpr int64_t kLargestWholeFile = 1 << 30;

// The Simple cache's header, by which a plain entry is told, and its entry's key
// is read (net/disk_cache/simple/simple_entry_format.h).
constexpr std::array<uint8_t, 8> kSimpleMagic = {0x30, 0x5c, 0x72, 0xa7,
                                                 0x1b, 0x6d, 0xfb, 0xfc};
constexpr size_t kSimpleKeyLengthOffset = 12;
constexpr size_t kSimpleHeaderSize = 24;

// What rebuilds on its own, whole, at any depth of a profile: gone rather than
// converted. Each cache's entries name the URLs they hold.
constexpr std::string_view kRebuilt[] = {
    "Cache",      "Code Cache",      "GPUCache",
    "DawnGraphiteCache", "DawnWebGPUCache", "Shared Dictionary",
    "No_Vary_Search",    "blob_storage",    "Download Service",
    "Sessions",   "Sessions_Encrypted",     "EncryptedBookmarks2",
};

// Files that are gone: stale, or memory-only now.
constexpr std::string_view kStale[] = {
    "Visited Links",
    "Network Persistent State",
    "SCT Auditing Pending Reports",
};

bool IsUnder(const base::FilePath& root,
             const base::FilePath& path,
             std::string_view name) {
  base::FilePath relative;

  if (!root.AppendRelativePath(path, &relative)) {
    return false;
  }

  const std::vector<std::string> parts = relative.GetComponents();
  return std::ranges::contains(parts, std::string(name));
}

std::vector<base::FilePath> FilesUnder(const base::FilePath& root) {
  std::vector<base::FilePath> files;
  base::FileEnumerator entries(root, /*recursive=*/true,
                               base::FileEnumerator::FILES);

  for (base::FilePath path = entries.Next(); !path.empty();
       path = entries.Next()) {
    files.push_back(path);
  }

  return files;
}

void SyncDirectory(const base::FilePath& directory) {
  base::File folder(directory, base::File::FLAG_OPEN | base::File::FLAG_READ);

  if (folder.IsValid()) {
    folder.Flush();
  }
}

bool ReadHead(const base::FilePath& path, base::span<uint8_t> head) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  return file.IsValid() && file.Read(0, head) == head.size();
}

// The temporary file, fsynced, in the original's place.
bool Replace(const base::FilePath& temporary, const base::FilePath& path) {
  {
    base::File file(temporary, base::File::FLAG_OPEN | base::File::FLAG_WRITE);

    if (!file.IsValid() || !file.Flush()) {
      return false;
    }
  }

  if (!base::ReplaceFile(temporary, path, nullptr)) {
    return false;
  }

  SyncDirectory(path.DirName());
  return true;
}

base::FilePath TemporaryFor(const base::FilePath& path) {
  return base::FilePath(base::StrCat({path.value(), kTemporarySuffix}));
}

ProfileKeys KeysFor(const base::FilePath& path) {
  PathVerdict verdict = encryption::Classify(path);
  CHECK(verdict.kind == PathKind::kEncrypted);
  return *verdict.keys;
}

// LevelDB --------------------------------------------------------------------

bool MigrateLevelDBFile(const base::FilePath& path) {
  const ProfileKeys keys = KeysFor(path);
  base::File source(path, base::File::FLAG_OPEN | base::File::FLAG_READ);

  if (!source.IsValid()) {
    return false;
  }

  const int64_t length = source.GetLength();

  if (length <= 0) {
    return true;
  }

  StreamCipher::Header head;

  if (length >= static_cast<int64_t>(head.size()) &&
      source.Read(0, head) == head.size() && StreamCipher::HasMagic(head)) {
    return true;
  }

  const base::FilePath temporary = TemporaryFor(path);
  std::unique_ptr<StreamCipher> cipher;
  StreamCipher::Header header;
  cipher = StreamCipher::ForNewFile(keys, &header);

  {
    base::File target(temporary, base::File::FLAG_CREATE_ALWAYS |
                                     base::File::FLAG_READ |
                                     base::File::FLAG_WRITE);

    if (!target.IsValid() || !target.WriteAtCurrentPosAndCheck(header)) {
      return false;
    }

    std::vector<uint8_t> plain(kBufferSize);
    std::vector<uint8_t> check(kBufferSize);
    std::vector<uint8_t> sealed(kBufferSize);
    uint64_t offset = 0;

    for (;;) {
      std::optional<size_t> read = source.Read(offset, plain);

      if (!read) {
        return false;
      }

      if (*read == 0) {
        break;
      }

      auto bytes = base::span(plain).first(*read);
      auto out = base::span(sealed).first(*read);
      cipher->Apply(offset, bytes, out);

      if (!target.WriteAtCurrentPosAndCheck(out)) {
        return false;
      }

      // Verified as it goes: what is on disk decrypts to the original.
      auto back = base::span(check).first(*read);

      if (target.Read(StreamCipher::kHeaderSize + offset, back) != *read) {
        return false;
      }

      cipher->Apply(offset, back, back);

      if (back != bytes) {
        return false;
      }

      offset += *read;
    }
  }

  return Replace(temporary, path);
}

// Whole files ----------------------------------------------------------------

bool MigrateWholeFile(const base::FilePath& path) {
  std::optional<std::vector<uint8_t>> bytes;

  {
    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);

    if (!file.IsValid()) {
      return false;
    }

    const int64_t length = file.GetLength();

    if (length == 0) {
      return true;
    }

    // base::File reads 2 GiB at most, and nothing Pane knows of is whole files
    // that large: it stays as it is, in the encrypted volume, rather than stop
    // the process at every launch. A plain file in a protected path passes.
    if (length > kLargestWholeFile) {
      LOG(ERROR) << "A file of a profile too large to seal whole.";
      return true;
    }

    bytes.emplace(length);

    if (file.Read(0, *bytes) != static_cast<size_t>(length)) {
      return false;
    }
  }

  if (encryption::IsSealedWholeFile(*bytes)) {
    return true;
  }

  const ProfileKeys keys = KeysFor(path);
  const std::string sealed = encryption::SealWholeFile(keys, *bytes);

  std::optional<std::string> opened =
      encryption::OpenWholeFile(keys, base::as_byte_span(sealed));

  if (!opened || base::as_byte_span(*opened) != base::span(*bytes)) {
    return false;
  }

  const base::FilePath temporary = TemporaryFor(path);
  base::File target(temporary,
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);

  if (!target.IsValid() ||
      !target.WriteAtCurrentPosAndCheck(base::as_byte_span(sealed))) {
    return false;
  }

  target.Close();
  return Replace(temporary, path);
}

// Stream files ---------------------------------------------------------------

// Whether it already is in the chunked format: what is plain never
// authenticates.
bool IsChunked(const base::FilePath& path) {
  std::unique_ptr<disk_cache::CacheFile> file = storage::PaneOpenEncryptedFile(
      path, base::File::FLAG_OPEN | base::File::FLAG_READ);

  if (!file->IsValid() || file->GetLength() <= 0) {
    return false;
  }

  std::array<uint8_t, 1> first;
  return file->Read(0, first) == first.size();
}

bool SameContent(const base::FilePath& plain, const base::FilePath& chunked) {
  base::File source(plain, base::File::FLAG_OPEN | base::File::FLAG_READ);
  std::unique_ptr<disk_cache::CacheFile> target =
      storage::PaneOpenEncryptedFile(
          chunked, base::File::FLAG_OPEN | base::File::FLAG_READ);

  if (!source.IsValid() || !target->IsValid() ||
      target->GetLength() != source.GetLength()) {
    return false;
  }

  std::vector<uint8_t> one(kBufferSize);
  std::vector<uint8_t> two(kBufferSize);

  for (int64_t offset = 0;;) {
    std::optional<size_t> read = source.Read(offset, one);

    if (!read) {
      return false;
    }

    if (*read == 0) {
      return true;
    }

    if (target->Read(offset, base::span(two).first(*read)) != *read ||
        base::span(one).first(*read) != base::span(two).first(*read)) {
      return false;
    }

    offset += *read;
  }
}

bool MigrateStreamFile(const base::FilePath& path) {
  base::File::Info info;

  if (!base::GetFileInfo(path, &info)) {
    return false;
  }

  if (info.size == 0 || IsChunked(path)) {
    return true;
  }

  const base::FilePath temporary = TemporaryFor(path);

  if (storage::PaneCopyInPlainFile(path, temporary) != base::File::FILE_OK ||
      !SameContent(path, temporary) ||
      // A page's blob is checked against its time.
      !base::TouchFile(temporary, info.last_accessed, info.last_modified)) {
    base::DeleteFile(temporary);
    return false;
  }

  return Replace(temporary, path);
}

// CacheStorage and the service workers' scripts ------------------------------

bool IsSimpleEntryName(std::string_view name) {
  return name.size() == 18 && name[16] == '_' &&
         std::all_of(name.begin(), name.begin() + 16, base::IsHexDigit<char>);
}

// A plain entry, in the chunked format, under its keyed name. The index goes,
// and rebuilds from the folder.
bool MigrateSimpleEntry(const base::FilePath& path) {
  std::array<uint8_t, kSimpleHeaderSize> header;

  if (!ReadHead(path, header) ||
      base::span(header).first<8u>() != base::span(kSimpleMagic)) {
    return true;
  }

  base::File source(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  const uint32_t key_length = base::U32FromLittleEndian(
      base::span(header).subspan<kSimpleKeyLengthOffset, 4u>());

  // A bad cache entry goes: nothing keeps it but a page that can ask again.
  if (!source.IsValid() || key_length > source.GetLength()) {
    return base::DeleteFile(path);
  }

  std::string key(key_length, '\0');

  if (source.Read(kSimpleHeaderSize, base::as_writable_byte_span(key)) !=
      key_length) {
    return base::DeleteFile(path);
  }

  std::unique_ptr<disk_cache::CacheEntryHasher> hasher;
  scoped_refptr<disk_cache::BackendFileOperationsFactory> factory;
  const disk_cache::PaneCacheHook* hook = disk_cache::GetPaneCacheHook();
  CHECK(hook && hook->wrap_cache(path, &factory, &hasher, /*wrapped=*/true));

  const std::string name = path.BaseName().AsUTF8Unsafe();
  const std::string keyed = base::StrCat(
      {base::ToLowerASCII(base::HexEncode(base::U64ToBigEndian(
           hasher->GetEntryHashKey(key)))),
       name.substr(16)});
  const base::FilePath target = path.DirName().AppendASCII(keyed);
  const base::FilePath temporary = TemporaryFor(target);

  if (storage::PaneCopyInPlainFile(path, temporary) != base::File::FILE_OK ||
      !SameContent(path, temporary) || !Replace(temporary, target)) {
    base::DeleteFile(temporary);
    return false;
  }

  return base::DeleteFile(path);
}

// Names ----------------------------------------------------------------------

// The string in field 2 (origin) or 3 (storage key) of a plain CacheStorage's
// index.txt, whichever comes first.
std::optional<std::string> OriginInIndex(const base::FilePath& index) {
  std::string bytes;

  if (!base::ReadFileToString(index, &bytes)) {
    return std::nullopt;
  }

  size_t at = 0;

  const auto varint = [&]() -> std::optional<uint64_t> {
    uint64_t value = 0;

    for (int shift = 0; at < bytes.size() && shift < 64; shift += 7) {
      const uint8_t byte = static_cast<uint8_t>(bytes[at++]);
      value |= static_cast<uint64_t>(byte & 0x7f) << shift;

      if (!(byte & 0x80)) {
        return value;
      }
    }

    return std::nullopt;
  };

  while (at < bytes.size()) {
    std::optional<uint64_t> tag = varint();

    if (!tag) {
      return std::nullopt;
    }

    const uint64_t field = *tag >> 3;

    switch (*tag & 7) {
      case 0:
        if (!varint()) {
          return std::nullopt;
        }
        break;
      case 2: {
        std::optional<uint64_t> length = varint();

        if (!length || *length > bytes.size() - at) {
          return std::nullopt;
        }

        if (field == 2 || field == 3) {
          return bytes.substr(at, *length);
        }

        at += *length;
        break;
      }
      default:
        return std::nullopt;
    }
  }

  return std::nullopt;
}

// CacheStorage's folders move from an unkeyed hash of their origin to a keyed
// name (ConstructOriginPath, patch 0010), each once.
bool MigrateCacheStorageNames(const base::FilePath& profile) {
  const base::FilePath root = profile.AppendASCII("Service Worker")
                                  .AppendASCII("CacheStorage");
  base::FileEnumerator folders(root, /*recursive=*/false,
                               base::FileEnumerator::DIRECTORIES);

  for (base::FilePath folder = folders.Next(); !folder.empty();
       folder = folders.Next()) {
    const base::FilePath index = folder.AppendASCII("index.txt");
    std::string head;

    // Sealed already: its folder has moved.
    if (!base::ReadFileToString(index, &head) ||
        encryption::IsSealedWholeFile(base::as_byte_span(head))) {
      continue;
    }

    std::optional<std::string> origin = OriginInIndex(index);

    if (!origin) {
      continue;
    }

    const std::string identifier = storage::GetIdentifierFromOrigin(
        url::Origin::Create(GURL(*origin)));
    std::optional<std::string> keyed =
        encryption::KeyedName(profile, identifier);

    if (!keyed) {
      return false;
    }

    const base::FilePath target = root.AppendASCII(*keyed);

    if (target != folder && !base::Move(folder, target)) {
      return false;
    }
  }

  return true;
}

// IndexedDB's folders move from one that names the origin to its default
// bucket's, as a third party's are (ShouldUseLegacyFilePath, patch 0010).
bool MigrateIndexedDBNames(const base::FilePath& profile) {
  const base::FilePath legacy = profile.AppendASCII("IndexedDB");

  if (!base::DirectoryExists(legacy)) {
    return true;
  }

  std::set<std::string> identifiers;
  base::FileEnumerator entries(legacy, /*recursive=*/false,
                               base::FileEnumerator::DIRECTORIES);

  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    const std::string name = entry.BaseName().AsUTF8Unsafe();

    for (const std::string_view suffix :
         {".indexeddb.leveldb", ".indexeddb.blob"}) {
      if (name.ends_with(suffix)) {
        identifiers.insert(name.substr(0, name.size() - suffix.size()));
      }
    }
  }

  if (identifiers.empty()) {
    return base::IsDirectoryEmpty(legacy) ? base::DeletePathRecursively(legacy)
                                          : true;
  }

  const base::FilePath web_storage = profile.AppendASCII("WebStorage");
  sql::Database quota(sql::DatabaseOptions(), "Quota");

  if (!base::CreateDirectory(web_storage) ||
      !quota.Open(web_storage.AppendASCII("QuotaManager"))) {
    LOG(ERROR) << "No quota database for IndexedDB's folders.";
    return false;
  }

  // Each default bucket, by the identifier its origin's folder is named by.
  std::map<std::string, int64_t> buckets;
  sql::Statement rows(quota.GetUniqueStatement(
      "SELECT id, storage_key FROM buckets WHERE name='_default'"));

  while (rows.Step()) {
    const GURL url(rows.ColumnStringView(1));

    if (url.is_valid()) {
      buckets[storage::GetIdentifierFromOrigin(url::Origin::Create(url))] =
          rows.ColumnInt64(0);
    }
  }

  for (const std::string& identifier : identifiers) {
    int64_t bucket = 0;

    if (auto known = buckets.find(identifier); known != buckets.end()) {
      bucket = known->second;
    } else {
      // A store the quota database lost track of keeps its data: scheme_host_port.
      const size_t scheme_end = identifier.find('_');
      const size_t host_end = identifier.rfind('_');

      if (scheme_end == std::string::npos || host_end <= scheme_end) {
        return false;
      }

      const std::string host =
          identifier.substr(scheme_end + 1, host_end - scheme_end - 1);
      const std::string port = identifier.substr(host_end + 1);
      const std::string storage_key = base::StrCat(
          {identifier.substr(0, scheme_end), "://", host,
           port == "0" ? "" : base::StrCat({":", port}), "/"});
      sql::Statement add(quota.GetUniqueStatement(
          "INSERT INTO buckets(storage_key, host, name, use_count, "
          "last_accessed, last_modified, expiration, quota, persistent, "
          "durability) VALUES(?, ?, '_default', 0, ?, ?, ?, 0, 0, 1)"));
      const base::Time now = base::Time::Now();
      add.BindString(0, storage_key);
      add.BindString(1, host);
      add.BindTime(2, now);
      add.BindTime(3, now);
      add.BindTime(4, base::Time::Max());

      if (!add.Run()) {
        return false;
      }

      bucket = quota.GetLastInsertRowId();
    }

    const base::FilePath target =
        web_storage.AppendASCII(base::NumberToString(bucket))
            .AppendASCII("IndexedDB");

    if (!base::CreateDirectory(target)) {
      return false;
    }

    for (const std::string_view extension : {"leveldb", "blob"}) {
      const base::FilePath from = legacy.AppendASCII(
          base::StrCat({identifier, ".indexeddb.", extension}));
      const base::FilePath to =
          target.AppendASCII(base::StrCat({"indexeddb.", extension}));

      if (base::PathExists(from) && !base::Move(from, to)) {
        return false;
      }
    }
  }

  quota.Close();
  return base::IsDirectoryEmpty(legacy) ? base::DeletePathRecursively(legacy)
                                        : true;
}

// The walk -------------------------------------------------------------------

bool IsLevelDBFolder(const base::FilePath& folder) {
  return base::PathExists(folder.AppendASCII("CURRENT"));
}

bool IsStreamPath(const base::FilePath& profile, const base::FilePath& path) {
  if (IsUnder(profile, path, "File System")) {
    return path.BaseName().value() != ".usage";
  }

  base::FilePath relative;
  profile.AppendRelativePath(path, &relative);

  for (const std::string& part : relative.GetComponents()) {
    if (part.ends_with(".blob")) {
      return true;
    }
  }

  return false;
}

bool IsSqliteSideFile(const base::FilePath& path) {
  const std::string name = path.BaseName().AsUTF8Unsafe();
  return name.ends_with("-journal") || name.ends_with("-wal") ||
         name.ends_with("-shm");
}

void RemoveWhatRebuilds(const base::FilePath& root) {
  base::FileEnumerator folders(root, /*recursive=*/true,
                               base::FileEnumerator::DIRECTORIES);
  std::vector<base::FilePath> gone;

  for (base::FilePath folder = folders.Next(); !folder.empty();
       folder = folders.Next()) {
    if (std::ranges::contains(kRebuilt, folder.BaseName().AsUTF8Unsafe()) &&
        !IsUnder(root, folder, "Extensions")) {
      gone.push_back(folder);
    }
  }

  for (const base::FilePath& folder : gone) {
    base::DeletePathRecursively(folder);
  }

  for (const std::string_view name : kStale) {
    base::DeleteFile(root.AppendASCII(name));
  }
}

}  // namespace

bool MigrateProfileFiles(const base::FilePath& profile,
                         const base::FilePath& cache,
                         base::RepeatingCallback<void(double)> progress) {
  RemoveWhatRebuilds(profile);
  base::DeletePathRecursively(cache);
  base::CreateDirectory(cache);

  std::vector<base::FilePath> files = FilesUnder(profile);
  uint64_t total = 1;
  uint64_t done = 0;

  for (const base::FilePath& file : files) {
    if (file.value().ends_with(kTemporarySuffix)) {
      base::DeleteFile(file);
    } else if (!IsUnder(profile, file, "Extensions")) {
      total += base::GetFileSize(file).value_or(0);
    }
  }

  const auto did = [&](const base::FilePath& file) {
    done += base::GetFileSize(file).value_or(0);
    progress.Run(std::min(1.0, static_cast<double>(done) / total));
  };

  // SQLite first: the quota database says where IndexedDB's folders go.
  bool ok = true;

  for (const base::FilePath& file : files) {
    if (!IsUnder(profile, file, "Extensions") &&
        encryption::IsPlainSqliteDatabase(file)) {
      switch (encryption::MigrateSqliteDatabase(file)) {
        case encryption::SqliteMigration::kFailed:
          LOG(ERROR) << "A database of a profile would not migrate.";
          ok = false;
          break;
        case encryption::SqliteMigration::kCorrupt:
          // Chromium would raze it at the page that shows it: one store, where
          // keeping it bars the whole profile, at every launch. The safety copy
          // keeps it.
          LOG(ERROR) << "A malformed database of a profile goes.";
          did(file);
          base::DeleteFile(file);
          base::DeleteFile(base::FilePath(file.value() + "-journal"));
          base::DeleteFile(base::FilePath(file.value() + "-wal"));
          base::DeleteFile(base::FilePath(file.value() + "-shm"));
          continue;
        case encryption::SqliteMigration::kMigrated:
        case encryption::SqliteMigration::kNothingToDo:
          break;
      }

      did(file);
    }
  }

  if (!ok || !MigrateIndexedDBNames(profile) ||
      !MigrateCacheStorageNames(profile)) {
    return false;
  }

  // Files may have moved, and the databases are done.
  files = FilesUnder(profile);

  for (const base::FilePath& file : files) {
    if (IsUnder(profile, file, "Extensions") ||
        encryption::IsEncryptedSqliteDatabase(file) ||
        encryption::IsPlainSqliteDatabase(file) || IsSqliteSideFile(file)) {
      continue;
    }

    const std::string name = file.BaseName().AsUTF8Unsafe();
    bool migrated = true;

    if (IsLevelDBFolder(file.DirName())) {
      // Its text log prints user keys, and takes nothing to rebuild.
      if (name == "LOG" || name == "LOG.old") {
        migrated = base::DeleteFile(file);
      } else if (name != "LOCK") {
        migrated = MigrateLevelDBFile(file);
      }
    } else if (IsStreamPath(profile, file)) {
      migrated = MigrateStreamFile(file);
    } else if (IsSimpleEntryName(name)) {
      migrated = MigrateSimpleEntry(file);
    } else if (name == "index" || name == "the-real-index" ||
               name == ".usage" || name == "LOG" || name == "LOG.old") {
      // A cache's index rebuilds from its folder; the rest hold nothing, or
      // are gone with no store around them.
      migrated = name == ".usage" || base::DeleteFile(file);
    } else {
      migrated = MigrateWholeFile(file);
    }

    if (!migrated) {
      LOG(ERROR) << "A file of a profile would not migrate: " << file.BaseName();
      return false;
    }

    did(file);
  }

  SyncDirectory(profile);
  progress.Run(1);
  return true;
}

bool WriteSafetyCopy(const base::FilePath& profile,
                     const base::FilePath& archive) {
  const ProfileKeys keys = KeysFor(archive);
  std::vector<base::FilePath> files;
  uint64_t total = 0;

  for (const base::FilePath& file : FilesUnder(profile)) {
    bool skipped = IsUnder(profile, file, "Extensions");

    for (const std::string_view name : kRebuilt) {
      skipped = skipped || IsUnder(profile, file, name);
    }

    if (!skipped) {
      files.push_back(file);
      total += base::GetFileSize(file).value_or(0);
    }
  }

  if (base::SysInfo::AmountOfFreeDiskSpace(archive.DirName()).value_or(0) <
      static_cast<int64_t>(total + (total / 2) + (100 << 20))) {
    LOG(ERROR) << "No room for a profile's safety copy.";
    return false;
  }

  const base::FilePath temporary = TemporaryFor(archive);
  base::File out(temporary,
                 base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  StreamCipher::Header header;
  std::unique_ptr<StreamCipher> cipher = StreamCipher::ForNewFile(keys, &header);

  if (!out.IsValid() || !out.WriteAtCurrentPosAndCheck(header)) {
    return false;
  }

  std::vector<uint8_t> buffer(kBufferSize);
  uint64_t offset = 0;

  const auto write = [&](base::span<const uint8_t> bytes) {
    std::vector<uint8_t> sealed(bytes.size());
    cipher->Apply(offset, bytes, sealed);
    offset += bytes.size();
    return out.WriteAtCurrentPosAndCheck(sealed);
  };

  for (const base::FilePath& file : files) {
    base::FilePath relative;
    profile.AppendRelativePath(file, &relative);
    const std::string name = relative.AsUTF8Unsafe();
    base::File source(file, base::File::FLAG_OPEN | base::File::FLAG_READ);
    base::File::Info info;

    if (!source.IsValid() || !source.GetInfo(&info)) {
      base::DeleteFile(temporary);
      return false;
    }

    if (!write(base::U32ToLittleEndian(name.size())) ||
        !write(base::as_byte_span(name)) ||
        !write(base::U64ToLittleEndian(info.size)) ||
        !write(base::U64ToLittleEndian(
            info.last_modified.ToDeltaSinceWindowsEpoch().InMicroseconds()))) {
      base::DeleteFile(temporary);
      return false;
    }

    for (uint64_t at = 0; at < static_cast<uint64_t>(info.size);) {
      std::optional<size_t> read = source.Read(at, buffer);

      if (!read || *read == 0 || !write(base::span(buffer).first(*read))) {
        base::DeleteFile(temporary);
        return false;
      }

      at += *read;
    }
  }

  out.Close();
  return Replace(temporary, archive);
}

bool RestoreSafetyCopy(const base::FilePath& profile,
                       const base::FilePath& archive) {
  const ProfileKeys keys = KeysFor(archive);
  base::File in(archive, base::File::FLAG_OPEN | base::File::FLAG_READ);
  StreamCipher::Header header;

  if (!in.IsValid() || in.Read(0, header) != header.size()) {
    return false;
  }

  std::unique_ptr<StreamCipher> cipher = StreamCipher::FromHeader(keys, header);
  const int64_t length = in.GetLength() - StreamCipher::kHeaderSize;
  int64_t offset = 0;

  const auto read = [&](base::span<uint8_t> bytes) {
    if (in.Read(StreamCipher::kHeaderSize + offset, bytes) != bytes.size()) {
      return false;
    }

    cipher->Apply(offset, bytes, bytes);
    offset += bytes.size();
    return true;
  };

  // Everything goes but the extensions' code, which no migration touches.
  base::FileEnumerator entries(profile, /*recursive=*/false,
                               base::FileEnumerator::FILES |
                                   base::FileEnumerator::DIRECTORIES);

  for (base::FilePath entry = entries.Next(); !entry.empty();
       entry = entries.Next()) {
    if (entry.BaseName().value() != "Extensions") {
      base::DeletePathRecursively(entry);
    }
  }

  std::vector<uint8_t> buffer(kBufferSize);

  while (offset < length) {
    std::array<uint8_t, 4> name_length;

    if (!read(name_length)) {
      return false;
    }

    std::string name(base::U32FromLittleEndian(name_length), '\0');
    std::array<uint8_t, 8> size_bytes;
    std::array<uint8_t, 8> time_bytes;

    if (!read(base::as_writable_byte_span(name)) || !read(size_bytes) ||
        !read(time_bytes)) {
      return false;
    }

    const base::FilePath target = profile.Append(base::FilePath::FromUTF8Unsafe(name));

    if (!base::CreateDirectory(target.DirName())) {
      return false;
    }

    base::File out(target,
                   base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    const uint64_t size = base::U64FromLittleEndian(size_bytes);

    for (uint64_t at = 0; at < size;) {
      auto chunk = base::span(buffer).first(
          std::min<size_t>(buffer.size(), size - at));

      if (!read(chunk) || !out.WriteAtCurrentPosAndCheck(chunk)) {
        return false;
      }

      at += chunk.size();
    }

    out.Close();
    const base::Time time = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(base::U64FromLittleEndian(time_bytes)));
    base::TouchFile(target, time, time);
  }

  return true;
}

}  // namespace pane
