#!/usr/bin/env python3
"""Rebuild StickS3 screenshot PNGs from a serial capture.

Device (build with -DS3_SCREENSHOT_BRIDGE) prints blocks like:

  S3SHOT_BEGIN <tag> <w> <h> <bytes>
  <base64 RGB565 little-endian, 76-col lines>
  S3SHOT_END <tag>

Usage:
  pio device monitor -b 115200 | tee capture.log
  # on device: show Normal / RECORD / PLAY screens
  python3 tools/frame_to_png.py capture.log -o docs/screenshots
"""

from __future__ import annotations

import argparse
import base64
import re
import struct
import zlib
from pathlib import Path

BEGIN_RE = re.compile(
    r"^S3SHOT_BEGIN\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$"
)
END_RE = re.compile(r"^S3SHOT_END\s+(\S+)\s*$")


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def rgb565_le_to_png(rgb565: bytes, width: int, height: int) -> bytes:
    expected = width * height * 2
    if len(rgb565) < expected:
        raise ValueError(
            f"buffer too short: got {len(rgb565)}, need {expected}"
        )
    rows = []
    off = 0
    for _ in range(height):
        row = bytearray([0])  # filter None
        for _ in range(width):
            # LGFX RGB565 framebuffer is big-endian in memory on ESP32.
            pix = (rgb565[off] << 8) | rgb565[off + 1]
            off += 2
            r = ((pix >> 11) & 0x1F) * 255 // 31
            g = ((pix >> 5) & 0x3F) * 255 // 63
            b = (pix & 0x1F) * 255 // 31
            row.extend((r, g, b))
        rows.append(bytes(row))
    raw = b"".join(rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(raw, 9))
        + png_chunk(b"IEND", b"")
    )


def extract_frames(text: str) -> list[tuple[str, int, int, bytes]]:
    frames: list[tuple[str, int, int, bytes]] = []
    tag = None
    w = h = nbytes = 0
    chunks: list[str] = []
    for line in text.splitlines():
        line = line.strip()
        m = BEGIN_RE.match(line)
        if m:
            tag = m.group(1)
            w, h, nbytes = int(m.group(2)), int(m.group(3)), int(m.group(4))
            chunks = []
            continue
        m = END_RE.match(line)
        if m and tag is not None:
            end_tag = m.group(1)
            if end_tag != tag:
                raise ValueError(f"tag mismatch: begin={tag} end={end_tag}")
            raw = base64.b64decode("".join(chunks), validate=False)
            if len(raw) != nbytes:
                raise ValueError(
                    f"{tag}: decoded {len(raw)} bytes, header said {nbytes}"
                )
            frames.append((tag, w, h, raw))
            tag = None
            chunks = []
            continue
        if tag is not None and line:
            chunks.append(line)
    return frames


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("capture", type=Path, help="serial log with S3SHOT_* blocks")
    ap.add_argument(
        "-o",
        "--outdir",
        type=Path,
        default=Path("docs/screenshots"),
        help="output directory for PNGs",
    )
    ap.add_argument(
        "--keep-all",
        action="store_true",
        help="keep every capture; default keeps last frame per tag",
    )
    args = ap.parse_args()

    frames = extract_frames(args.capture.read_text(errors="replace"))
    if not frames:
        print("no S3SHOT blocks found")
        return 1

    if not args.keep_all:
        latest: dict[str, tuple[str, int, int, bytes]] = {}
        for fr in frames:
            latest[fr[0]] = fr
        frames = list(latest.values())

    args.outdir.mkdir(parents=True, exist_ok=True)
    counts: dict[str, int] = {}
    for tag, w, h, raw in frames:
        n = counts.get(tag, 0)
        counts[tag] = n + 1
        suffix = f"_{n}" if args.keep_all and n else ""
        safe = re.sub(r"[^A-Za-z0-9._-]+", "_", tag)
        out = args.outdir / f"{safe}{suffix}.png"
        out.write_bytes(rgb565_le_to_png(raw, w, h))
        print(f"wrote {out} ({w}x{h})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
