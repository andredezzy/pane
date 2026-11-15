#!/usr/bin/env bash
# Publishes the engine this checkout's sources name, for one platform, for
# the release workflow to fetch (fetch-engine.sh): its archive on the
# engine-<key> GitHub prerelease, then its line in prebuilt-engines.tsv,
# which the caller commits. CI never builds Chromium (release.yml), so an
# engine change is built here first (build.sh) and published with this, once
# for each platform. The Mac's archive (mac-arm64, the default) is the
# engine's bundle, stripped, with its framework at its top for the linker and
# the volume's watchdog beside it, which package.sh puts in the app, and
# PaneCore's dSYM beside it; Windows' (windows-x64) is the engine's files
# that package-win.sh lays out (windows-engine-files.sh). Each made when
# missing and only from the engine build.sh built from these sources; the
# upload is checked against its SHA-256. A run for an engine already
# published changes nothing.
# Usage: publish-engine.sh [mac-arm64|windows-x64]   (gh, signed in with write
# access to the repo)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RELEASE="$ROOT_DIR/packaging/release"
TSV="$ROOT_DIR/chromium/prebuilt-engines.tsv"
PLATFORM="${1:-mac-arm64}"

die() {
	echo "error: $*" >&2
	exit 1
}

case "$PLATFORM" in
mac-arm64)
	ENGINE_OUT="${PANE_ENGINE_OUT:-$HOME/chromium/src/out/Release}"
	suffix=""
	;;
windows-x64)
	ENGINE_OUT="${PANE_WIN_ENGINE_OUT:-$HOME/chromium/src/out/Win}"
	suffix="-windows-x64"
	;;
*) die "no platform $PLATFORM: mac-arm64 or windows-x64" ;;
esac

# Refuses an engine other than the one build.sh built from these sources:
# one built before the last change to them, or from a checkout whose patches
# or //pane were not these, has another key in pane-engine.key, or none.
check_built_key() {
	local built_key
	built_key="$(cat "$ENGINE_OUT/pane-engine.key" 2>/dev/null || true)"
	[ -n "$built_key" ] || die "$ENGINE_OUT records no engine key: build it with build.sh before archiving it"
	[ "$built_key" = "$key" ] || die "the engine in $ENGINE_OUT was built from other sources (key $built_key) than this checkout's ($key): build it again"
}

# Archives Windows' engine in out/Win: the files package-win.sh lays out, at
# their paths there.
archive_windows_engine() {
	local stage
	check_built_key
	[ -f "$ENGINE_OUT/$version.manifest" ] || die "the engine in $ENGINE_OUT is not $version: it has no $version.manifest"

	stage="$(mktemp -d)"
	"$ROOT_DIR/scripts/windows-engine-files.sh" "$ENGINE_OUT" | while IFS= read -r relative; do
		mkdir -p "$stage/$(dirname "$relative")"
		cp -p "$ENGINE_OUT/$relative" "$stage/$relative"
	done

	mkdir -p "$RELEASE"
	rm -f "$archive"
	ditto -c -k --norsrc --noextattr --noacl "$stage" "$archive"
	rm -rf "$stage"
	echo "$(shasum -a 256 "$archive" | awk '{ print $1 }')  $name" > "$archive.sha256"
	echo "archived $archive"
}

# Archives the Mac's engine in out/Release.
archive_engine() {
	local built stage symbols dsym_stage
	check_built_key

	built="$(plutil -extract CFBundleShortVersionString raw "$ENGINE_OUT/Pane.app/Contents/Info.plist")"
	[ "$built" = "$version" ] || die "the engine in $ENGINE_OUT is $built, not $version"
	[ -x "$ENGINE_OUT/pane-volume-watch" ] || die "$ENGINE_OUT has no pane-volume-watch: build the engine with build.sh"

	stage="$(mktemp -d)"
	ditto "$ENGINE_OUT/Pane.app" "$stage/Pane.app"
	ln -s "Pane.app/Contents/Frameworks/Pane Framework.framework" "$stage/Pane Framework.framework"
	cp "$ENGINE_OUT/pane-volume-watch" "$stage/pane-volume-watch"
	strip -x "$stage/Pane Framework.framework/Versions/Current/Pane Framework" "$stage/Pane Framework.framework/Versions/Current"/Libraries/*.dylib \
		2> >(grep -v 'will invalidate the code signature' >&2)

	mkdir -p "$RELEASE"
	rm -f "$archive"
	ditto -c -k --norsrc --noextattr --noacl "$stage" "$archive"
	rm -rf "$stage"
	echo "$(shasum -a 256 "$archive" | awk '{ print $1 }')  $name" > "$archive.sha256"
	echo "archived $archive"

	# The engine's debug symbols, which the archive's stripped PaneCore lacks,
	# so a crash inside the engine reads too: dsymutil gathers them from the
	# build's objects, which the next build changes (157: about a minute and
	# 10 GB of memory, 2 GB, 0.5 GB zipped). Its UUID is the shipped PaneCore's.
	symbols="${archive%.zip}.dSYM.zip"
	dsym_stage="$(mktemp -d)"
	nice -n 10 dsymutil -o "$dsym_stage/Pane Framework.dSYM" "$ENGINE_OUT/Pane.app/Contents/Frameworks/Pane Framework.framework/Versions/Current/Pane Framework"
	rm -f "$symbols"
	ditto -c -k --keepParent "$dsym_stage/Pane Framework.dSYM" "$symbols"
	rm -rf "$dsym_stage"
	echo "symbols $symbols"
}

key="$("$ROOT_DIR/scripts/engine-key.sh")"
short="${key:0:12}"
version="$(tr -d '[:space:]' < "$ROOT_DIR/chromium/CHROMIUM_VERSION")"
tag="engine-$short"
name="PaneCore-$version-$short$suffix.zip"
archive="$RELEASE/$name"

if awk -v key="$key" -v platform="$PLATFORM" '$1 == key && $2 == platform { found = 1 } END { exit !found }' "$TSV"; then
	echo "$tag's $PLATFORM engine is published: prebuilt-engines.tsv names it"
	exit 0
fi

# The tag marks the commit that last changed the engine's sources, on GitHub.
commit="$(git -C "$ROOT_DIR" log -1 --format=%H -- chromium/CHROMIUM_VERSION chromium/args chromium/patches pane)"
git -C "$ROOT_DIR" fetch --quiet origin
git -C "$ROOT_DIR" branch -r --contains "$commit" | grep -q . ||
	die "$(git -C "$ROOT_DIR" log -1 --format='%h %s' "$commit") changed the engine and is not on GitHub: push it first"

if [ ! -f "$archive" ] || [ ! -f "$archive.sha256" ]; then
	echo "== archive"

	if [ "$PLATFORM" = windows-x64 ]; then
		archive_windows_engine >&2
	else
		archive_engine >&2
	fi
fi

(cd "$RELEASE" && shasum -a 256 -c --quiet "$name.sha256") || die "$name does not match its SHA-256"
sha256="$(cut -d' ' -f1 < "$archive.sha256")"

if gh release view "$tag" >/dev/null 2>&1; then
	echo "== $tag is on GitHub"

	# A platform's archive joins its key's prerelease.
	if ! gh release view "$tag" --json assets --jq '.assets[].name' | grep -qxF "$name"; then
		echo "== upload $name"
		gh release upload "$tag" "$archive"
	fi
else
	echo "== publish $tag"
	# A prerelease, never the latest: the app's update check reads the latest
	# release.
	gh release create "$tag" "$archive" \
		--prerelease \
		--latest=false \
		--target "$commit" \
		--title "Engine $short" \
		--notes "PaneCore $version for the engine sources $key, built from $(git -C "$ROOT_DIR" log -1 --format=%h "$commit"). The engine the release workflow fetches (scripts/fetch-engine.sh); not an app release."
fi

uploaded="$(gh release view "$tag" --json assets --jq ".assets[] | select(.name == \"$name\") | .digest")"
[ "$uploaded" = "sha256:$sha256" ] || die "$tag's $name is ${uploaded:-missing} on GitHub, not sha256:$sha256"

# PaneCore's debug symbols, for reading a crash inside the engine
# (docs/building.md), when this script made them with the archive.
symbols="${archive%.zip}.dSYM.zip"

if [ -f "$symbols" ] && ! gh release view "$tag" --json assets --jq '.assets[].name' | grep -qxF "$(basename "$symbols")"; then
	echo "== upload $(basename "$symbols")"
	gh release upload "$tag" "$symbols"
fi

# This platform's line, beside the other platforms' of the same key; a line
# of another key names an engine these sources no longer build.
{
	grep '^#' "$TSV"
	{
		awk -v key="$key" -v platform="$PLATFORM" '!/^#/ && $1 == key && $2 != platform' "$TSV"
		printf '%s\t%s\t%s\n' "$key" "$PLATFORM" "$sha256"
	} | sort -t "$(printf '\t')" -k2,2
} > "$TSV.new"
mv "$TSV.new" "$TSV"
echo "published $tag's $PLATFORM engine; prebuilt-engines.tsv names it: commit it"
