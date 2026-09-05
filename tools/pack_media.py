#!/usr/bin/env python3
"""Pack MJPEG + MP3 clip pairs into media.bin for the C6 video player.

Usage: pack_media.py out.bin fps clip1.mjpeg clip1.mp3 [clip2.mjpeg clip2.mp3 ...]

Layout (all little-endian uint32):
  header : magic "MDLA", version, nclips, reserved
  clips  : nclips x { fps_num, fps_den, nframes, index_off, max_frame,
                      mp3_off, mp3_size, sample_rate }
  index  : per clip, nframes x { off, size }   (offsets from file start)
  data   : JPEG frames, then MP3 stream, per clip
"""
import struct
import sys


def split_mjpeg(data: bytes):
    frames = []
    pos = 0
    n = len(data)
    while True:
        soi = data.find(b"\xff\xd8", pos)
        if soi < 0:
            break
        # scan entropy data for EOI, skipping stuffed FF00 and RSTn markers
        p = soi + 2
        end = -1
        while p < n - 1:
            if data[p] == 0xFF:
                m = data[p + 1]
                if m == 0xD9:
                    end = p + 2
                    break
                if m == 0x00 or 0xD0 <= m <= 0xD7 or m == 0xFF:
                    p += 2 if m != 0xFF else 1
                    continue
                if m == 0xDA:  # SOS: skip header, then entropy data follows
                    seglen = struct.unpack(">H", data[p + 2:p + 4])[0]
                    p += 2 + seglen
                    continue
                if 0xC0 <= m <= 0xFE:  # other marker segments carry a length
                    seglen = struct.unpack(">H", data[p + 2:p + 4])[0]
                    p += 2 + seglen
                    continue
            p += 1
        if end < 0:
            break
        frames.append((soi, end - soi))
        pos = end
    return frames


def mp3_sample_rate(data: bytes) -> int:
    rates = {0: [44100, 48000, 32000], 2: [22050, 24000, 16000], 1: [11025, 12000, 8000]}
    for i in range(len(data) - 4):
        if data[i] == 0xFF and (data[i + 1] & 0xE0) == 0xE0:
            ver = (data[i + 1] >> 3) & 3   # 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5
            layer = (data[i + 1] >> 1) & 3
            sr_idx = (data[i + 2] >> 2) & 3
            if ver == 1 or layer != 1 or sr_idx == 3:
                continue
            key = {3: 0, 2: 2, 0: 1}[ver]
            return rates[key][sr_idx]
    raise SystemExit("no MP3 frame header found")


def main():
    if len(sys.argv) < 5 or (len(sys.argv) - 3) % 2:
        raise SystemExit(__doc__)
    out_path = sys.argv[1]
    fps = float(sys.argv[2])
    pairs = list(zip(sys.argv[3::2], sys.argv[4::2]))
    fps_num, fps_den = (int(round(fps * 1000)), 1000)

    clips = []
    for mj_path, mp3_path in pairs:
        mj = open(mj_path, "rb").read()
        mp3 = open(mp3_path, "rb").read()
        frames = split_mjpeg(mj)
        if not frames:
            raise SystemExit(f"{mj_path}: no JPEG frames found")
        clips.append(dict(mj=mj, mp3=mp3, frames=frames, sr=mp3_sample_rate(mp3)))

    HDR = 16
    CLIP = 32
    pos = HDR + CLIP * len(clips)
    for c in clips:
        c["index_off"] = pos
        pos += 8 * len(c["frames"])
    for c in clips:
        c["frames_base"] = pos
        pos += sum(sz for _, sz in c["frames"])
        c["mp3_off"] = pos
        pos += len(c["mp3"])
        pos = (pos + 3) & ~3

    with open(out_path, "wb") as f:
        f.write(b"MDLA" + struct.pack("<III", 1, len(clips), 0))
        for c in clips:
            f.write(struct.pack("<8I", fps_num, fps_den, len(c["frames"]), c["index_off"],
                                max(sz for _, sz in c["frames"]), c["mp3_off"],
                                len(c["mp3"]), c["sr"]))
        for c in clips:
            off = c["frames_base"]
            for _, sz in c["frames"]:
                f.write(struct.pack("<II", off, sz))
                off += sz
        for c in clips:
            for src, sz in c["frames"]:
                f.write(c["mj"][src:src + sz])
            f.write(c["mp3"])
            f.write(b"\0" * ((-f.tell()) & 3))
        total = f.tell()

    for i, (c, (mj_path, _)) in enumerate(zip(clips, pairs)):
        n = len(c["frames"])
        print(f"clip {i}: {mj_path}: {n} frames ({n / fps:.1f} s), "
              f"video {sum(s for _, s in c['frames']) / 1e6:.2f} MB, "
              f"max frame {max(s for _, s in c['frames'])} B, mp3 {len(c['mp3']) / 1e6:.2f} MB @ {c['sr']} Hz")
    print(f"wrote {out_path}: {total} bytes ({total / 1e6:.2f} MB)")


if __name__ == "__main__":
    main()
