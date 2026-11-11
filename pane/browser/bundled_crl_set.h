// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_BUNDLED_CRL_SET_H_
#define PANE_BROWSER_BUNDLED_CRL_SET_H_

#include <optional>

namespace base {
class FilePath;
}

namespace pane {

// The CRLSet the app carries (ADR 0111): package.sh puts the current one in
// the bundle's resources as the app is built. Nullopt for a build that carries
// none, as out/Release's doesn't.
std::optional<base::FilePath> BundledCRLSet();

}  // namespace pane

#endif  // PANE_BROWSER_BUNDLED_CRL_SET_H_
