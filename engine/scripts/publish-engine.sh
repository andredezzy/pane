#!/usr/bin/env bash
# Publishes the engine this checkout's sources name, for CI and the release
# workflow to fetch (fetch-engine.sh): its archive as the engine-<key> GitHub
# prerelease, then its line in prebuilt-engines.tsv, which the caller
# commits. CI never builds Chromium (app.yml), so an engine change is built
# here first (build.sh) and published with this. The archive is package.sh's,
# made when missing; the upload is checked against its SHA-256. A run for an
# engine already published changes nothing.
# Usage: publish-engine.sh   (gh, signed in with write access to the repo)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
ENGINE_DIR="$ROOT_DIR/engine"
RELEASE="$ROOT_DIR/app/release"
TSV="$ENGINE_DIR/prebuilt-engines.tsv"

die() {
	echo "error: $*" >&2
	exit 1
}

key="$("$ENGINE_DIR/scripts/engine-key.sh")"
short="${key:0:12}"
version="$(tr -d '[:space:]' < "$ENGINE_DIR/CHROMIUM_VERSION")"
tag="engine-$short"
name="PaneCore-$version-$short.zip"
archive="$RELEASE/$name"

if awk -v key="$key" '$1 == key { found = 1 } END { exit !found }' "$TSV"; then
	echo "$tag is published: prebuilt-engines.tsv names it"
	exit 0
fi

# The tag marks the commit that last changed the engine's sources, on GitHub.
commit="$(git -C "$ROOT_DIR" log -1 --format=%H -- engine/CHROMIUM_VERSION engine/args engine/patches engine/pane)"
git -C "$ROOT_DIR" fetch --quiet origin
git -C "$ROOT_DIR" branch -r --contains "$commit" | grep -q . ||
	die "$(git -C "$ROOT_DIR" log -1 --format='%h %s' "$commit") changed the engine and is not on GitHub: push it first"

if [ ! -f "$archive" ] || [ ! -f "$archive.sha256" ]; then
	echo "== archive"
	PANE_ENGINE_ARCHIVE=1 PANE_APP_OUT="$(mktemp -d)/Pane.app" "$ENGINE_DIR/scripts/package.sh" >&2
fi

(cd "$RELEASE" && shasum -a 256 -c --quiet "$name.sha256") || die "$name does not match its SHA-256"
sha256="$(cut -d' ' -f1 < "$archive.sha256")"

if gh release view "$tag" >/dev/null 2>&1; then
	echo "== $tag is on GitHub"
else
	echo "== publish $tag"
	# A prerelease, never the latest: the app's update check reads the latest
	# release.
	gh release create "$tag" "$archive" \
		--prerelease \
		--latest=false \
		--target "$commit" \
		--title "Engine $short" \
		--notes "PaneCore $version for the engine sources $key, built from $(git -C "$ROOT_DIR" log -1 --format=%h "$commit"). The engine CI and the release workflow fetch (engine/scripts/fetch-engine.sh); not an app release."
fi

uploaded="$(gh release view "$tag" --json assets --jq ".assets[] | select(.name == \"$name\") | .digest")"
[ "$uploaded" = "sha256:$sha256" ] || die "$tag's $name is ${uploaded:-missing} on GitHub, not sha256:$sha256"

printf '%s\t%s\t%s\n' "$key" "$tag" "$sha256" >> "$TSV"
echo "published $tag; prebuilt-engines.tsv names it: commit it"
