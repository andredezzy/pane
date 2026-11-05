// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/handover.h"

#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/memory/unsafe_shared_memory_region.h"
#include "base/memory/writable_shared_memory_region.h"
#include "pane/encryption/engine_keys.h"
#include "pane/encryption/sqlite_vfs.h"
#include "pane/encryption/whole_file.h"
#include "third_party/boringssl/src/include/openssl/mem.h"

namespace pane::encryption {

mojom::EngineKeysPtr KeysForOtherProcess() {
  std::vector<base::FilePath> roots = Roots();

  if (roots.empty()) {
    return nullptr;
  }

  auto handover = mojom::EngineKeys::New();
  handover->roots = std::move(roots);

  for (auto& [id, key] : RegisteredKeys()) {
    base::UnsafeSharedMemoryRegion region =
        base::UnsafeSharedMemoryRegion::Create(key.size());
    CHECK(region.IsValid());
    base::WritableSharedMemoryMapping mapping = region.Map();
    CHECK(mapping.IsValid());
    mapping.GetMemoryAsSpan<uint8_t>().copy_from(base::span(key));
    OPENSSL_cleanse(key.data(), key.size());
    handover->keys.push_back(mojom::ProfileKey::New(id, std::move(region)));
  }

  return handover;
}

void ReceiveKeys(mojom::EngineKeysPtr keys, Stores stores) {
  if (!keys) {
    return;
  }

  if (stores == Stores::kAll) {
    InstallHooks();
  }

  AddRoots(keys->roots);

  for (const mojom::ProfileKeyPtr& entry : keys->keys) {
    base::WritableSharedMemoryMapping mapping = entry->key.Map();
    CHECK(mapping.IsValid());
    base::span<uint8_t> bytes = mapping.GetMemoryAsSpan<uint8_t>();
    CHECK_EQ(bytes.size(), kKeySize);
    RegisterKey(entry->id, bytes.first<kKeySize>());
    OPENSSL_cleanse(bytes.data(), bytes.size());
  }
}

void InstallHooks() {
  InstallWholeFileCodec();
  InstallSqliteVfs();
}

}  // namespace pane::encryption
