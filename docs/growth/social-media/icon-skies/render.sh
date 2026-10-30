#!/usr/bin/env bash
# Renders the icon-skies video from Pane's current app icons:
# out/pane-icon-skies-4k.mp4 (3840×2160, 60 fps, 10 s, the one to post) and
# out/pane-icon-skies-1080p.mp4. About a minute on an M-series Mac.
# Needs ffmpeg (brew install ffmpeg), Xcode's Swift, and Paper.app installed
# (its icon sits in the dock). The aerial is macOS's own Golden Gate Sunset
# wallpaper; when this Mac has not downloaded it, it comes from Apple.
# Usage: render.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$HERE/../../../.." && pwd)"
BUILD="$HERE/build"
OUT="$HERE/out"
AERIAL="$HOME/Library/Application Support/com.apple.wallpaper/aerials/videos/4207734D-74FE-4F92-B5E1-6EC8DEE24A15.mov"
AERIAL_URL="https://sylvan.apple.com/itunes-assets/Aerials116/v4/cb/5b/50/cb5b5035-6701-619f-9065-3d7d0e5fbef4/GG_A_SUNSET_MarshallsBeach_c28_v7_24comp_HFR_16Mbps.mov"

command -v ffmpeg > /dev/null || { echo "ffmpeg is missing: brew install ffmpeg" >&2; exit 1; }
mkdir -p "$BUILD" "$OUT"

if [[ ! -f "$AERIAL" ]]; then
	AERIAL="$BUILD/golden-gate-sunset.mov"
	[[ -f "$AERIAL" ]] || curl -fL --progress-bar -o "$AERIAL" "$AERIAL_URL"
fi

swift "$HERE/rhythm.swift" "$BUILD"
swift "$HERE/app-icons.swift" "$BUILD"
swiftc -O -o "$BUILD/compositor" "$HERE/compositor.swift"

# The aerial from 0:05, cropped to 16:9, one 240 fps frame per 60 fps frame
# (4× slow motion). The background cuts on frame 552, the landing on Day: the
# frames before it are the aerial's 48 to 599 and the ones after are 0 to 47,
# so the last frame runs on into the first and the loop has no seam.
ffmpeg -loglevel error -ss 5 -t 2.6 -i "$AERIAL" -filter_complex "\
[0:v]crop=3840:2160:128:0,setpts=N/(60*TB),fps=60,format=rgba,split[s1][s2];\
[s1]trim=start_frame=48:end_frame=600,setpts=PTS-STARTPTS[a];\
[s2]trim=end_frame=48,setpts=PTS-STARTPTS[b];\
[a][b]concat=n=2:v=1:a=0,format=rgba[out]" -map "[out]" -frames:v 600 -f rawvideo -pix_fmt rgba - \
	| "$BUILD/compositor" "$ROOT_DIR" "$BUILD" \
	| ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s 3840x2160 -r 60 -i - -i "$BUILD/audio.wav" \
		-map 0:v -map 1:a -c:v libx264 -preset slow -crf 12 -profile:v high -level 5.2 -pix_fmt yuv420p \
		-colorspace bt709 -color_primaries bt709 -color_trc bt709 -c:a aac -b:a 256k -shortest -movflags +faststart \
		"$OUT/pane-icon-skies-4k.mp4"

ffmpeg -loglevel error -y -i "$OUT/pane-icon-skies-4k.mp4" -vf scale=1920:1080:flags=lanczos \
	-c:v libx264 -preset slow -crf 14 -profile:v high -pix_fmt yuv420p -c:a copy -movflags +faststart \
	"$OUT/pane-icon-skies-1080p.mp4"

echo "$OUT/pane-icon-skies-4k.mp4"
