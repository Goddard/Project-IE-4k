#!/usr/bin/env bash

set -euo pipefail

SRC_DIR="assets/gemrb-demo/full-size"
DST_DIR="assets/gemrb-demo/thumb-nail"
WIDTH="480"

cd "$(git rev-parse --show-toplevel)"

if [[ ! -d "$SRC_DIR" ]]; then
  echo "ERROR: Source dir not found: $SRC_DIR" >&2
  exit 1
fi
mkdir -p "$DST_DIR"

# Prefer ImageMagick 'magick' or 'convert'; fallback to ffmpeg (very fast)
if command -v magick >/dev/null 2>&1; then
  CONVERT_CMD="magick"
elif command -v convert >/dev/null 2>&1; then
  CONVERT_CMD="convert"
elif command -v ffmpeg >/dev/null 2>&1; then
  CONVERT_CMD="ffmpeg"
else
  echo "ERROR: Need 'magick' (ImageMagick) or 'ffmpeg' installed." >&2
  exit 1
fi

shopt -s nullglob nocaseglob
for src in "$SRC_DIR"/*.{png,jpg,jpeg}; do
  base="$(basename "$src")"
  dst="$DST_DIR/$base"

  # Skip if up-to-date
  if [[ -f "$dst" && "$dst" -nt "$src" ]]; then
    continue
  fi

  echo "Thumbnail: $base"
  if [[ "$CONVERT_CMD" == "ffmpeg" ]]; then
    # Fast scaling with ffmpeg; preserve aspect ratio to width, auto height
    ffmpeg -v error -y -i "$src" -vf "scale=$WIDTH:-1:flags=bicubic" -q:v 4 "$dst"
  else
    # ImageMagick: fast resize + slight sharpening
    "$CONVERT_CMD" "$src" -resize ${WIDTH}x -unsharp 0x0.75+0.75+0.008 "$dst"
  fi
done

echo "Done. Thumbnails in $DST_DIR"


