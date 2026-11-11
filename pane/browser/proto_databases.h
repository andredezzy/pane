// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_PROTO_DATABASES_H_
#define PANE_BROWSER_PROTO_DATABASES_H_

namespace leveldb_proto {
class ProtoDatabaseProvider;
}  // namespace leveldb_proto

namespace pane {

// A profile's shared proto database (leveldb_proto's `shared_proto_db` and its
// `metadata`, two LevelDBs in the profile's folder) closes as soon as its open
// has ended, once `provider` has gone with the profile: a lock's detach waits
// for its files (ADR 0116). Chrome opens it as the profile starts. Its open,
// once ended, posts the clean-up of obsolete clients two minutes on, holding
// the database until then, and the provider's Shutdown, as the profile goes,
// cancels that clean-up. A profile that went while the open was still under
// way had its Shutdown first, with nothing yet to cancel, and the database
// stayed open two minutes past it: the lock's detach gave up after 10 s, and
// Pane quit. The clean-up made immediate runs as the open ends, and the
// database closes after it. Chromium's own seam, which its tests use; it
// changes nothing else. Called as the profile's services shut down, right
// before its storage goes (profiles.cc). Thread-safe.
void LetSharedProtoDatabaseGo(leveldb_proto::ProtoDatabaseProvider& provider);

}  // namespace pane

#endif  // PANE_BROWSER_PROTO_DATABASES_H_
