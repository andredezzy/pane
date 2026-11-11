// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/proto_databases.h"

#include "base/time/time.h"
#include "components/leveldb_proto/public/proto_database_provider.h"

namespace pane {

void LetSharedProtoDatabaseGo(leveldb_proto::ProtoDatabaseProvider& provider) {
  provider.SetSharedDBDeleteObsoleteDelayForTesting(base::TimeDelta());
}

}  // namespace pane
