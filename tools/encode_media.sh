#!/bin/bash
# Encode the easter-egg clips and pack media/media.bin (flash it at 0x110000, the "media" partition).
# Usage: tools/encode_media.sh clip1.mp4 [clip2.mp4 ...]
# Frames: 240x136 MJPEG (upright, letterboxed on the panel), 24 fps, quality 4. Audio: 32 kHz mono 64 kbps MP3.
set -e
cd "$(dirname "$0")/.."
mkdir -p media
FPS=24; Q=4
args=(); i=0
for f in "$@"; do
  i=$((i+1))
  ffmpeg -y -v error -i "$f" -vf "scale=240:136" -vcodec mjpeg -q:v $Q -r $FPS -pix_fmt yuvj420p -an media/clip$i.mjpeg
  ffmpeg -y -v error -i "$f" -vn -ac 1 -ar 32000 -b:a 64k -codec:a libmp3lame \
      -map_metadata -1 -id3v2_version 0 -write_xing 0 -f mp3 media/clip$i.mp3
  args+=(media/clip$i.mjpeg media/clip$i.mp3)
done
python3 tools/pack_media.py media/media.bin $FPS "${args[@]}"
