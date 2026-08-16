#!/usr/bin/env python3
"""Fetch the printer's live LVGL screen over WiFi and save/show it as a PNG.

    python tools/view_screen.py                     # save screen.png and open it
    python tools/view_screen.py --host 192.168.4.1  # AP mode address
    python tools/view_screen.py --nav 6             # switch screen, then capture
    python tools/view_screen.py --loop 2            # refresh every 2s

Stream format from GET /api/screenshot:
    "L3DP", u16 width, u16 height          (little-endian)
    then per flushed area:
      u16 x1, y1, x2, y2, then w*h RGB565 pixels, big-endian
Areas may arrive in any order; they are composited into the image.
"""

import argparse
import os
import struct
import sys
import time

import requests
from PIL import Image

DEFAULT_HOST = "lite3dp.local"


def rgb565_be_to_rgb(buf):
    """Convert big-endian RGB565 bytes to a list of (r, g, b) tuples."""
    out = []
    for hi, lo in zip(buf[0::2], buf[1::2]):
        v = (hi << 8) | lo
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        # Scale to 8 bits, replicating high bits into the low ones
        out.append(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return out


def auth_headers(key):
    return {"X-Api-Key": key} if key else {}


def fetch_screen(host, key=None, timeout=20):
    url = f"http://{host}/api/screenshot"
    resp = requests.get(url, headers=auth_headers(key), timeout=timeout)
    if resp.status_code == 409:
        raise RuntimeError("printer is mid-print; the panel is showing the mask")
    resp.raise_for_status()
    data = resp.content

    if len(data) < 8 or data[:4] != b"L3DP":
        raise RuntimeError(f"unexpected response ({len(data)} bytes, no L3DP magic)")

    width, height = struct.unpack_from("<HH", data, 4)
    img = Image.new("RGB", (width, height), (0, 0, 0))

    off = 8
    areas = 0
    while off + 8 <= len(data):
        x1, y1, x2, y2 = struct.unpack_from("<HHHH", data, off)
        off += 8
        aw, ah = x2 - x1 + 1, y2 - y1 + 1
        npx = aw * ah
        if aw <= 0 or ah <= 0 or off + npx * 2 > len(data):
            print(f"  truncated area at offset {off}, stopping", file=sys.stderr)
            break
        pixels = rgb565_be_to_rgb(data[off:off + npx * 2])
        off += npx * 2
        tile = Image.new("RGB", (aw, ah))
        tile.putdata(pixels)
        img.paste(tile, (x1, y1))
        areas += 1

    print(f"{width}x{height}, {areas} areas, {len(data)} bytes", file=sys.stderr)
    return img


def navigate(host, screen, key=None, timeout=10):
    r = requests.get(f"http://{host}/api/ui/nav", params={"screen": screen},
                     headers=auth_headers(key), timeout=timeout)
    r.raise_for_status()
    print(f"navigated: {r.text}", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=DEFAULT_HOST,
                    help=f"printer host or IP (default {DEFAULT_HOST})")
    ap.add_argument("-o", "--out", default="screen.png", help="output PNG path")
    ap.add_argument("--nav", type=int, metavar="N",
                    help="navigate to screen N before capturing")
    ap.add_argument("--loop", type=float, metavar="SEC",
                    help="re-capture every SEC seconds until interrupted")
    ap.add_argument("--no-show", action="store_true",
                    help="just write the file, don't open a viewer")
    ap.add_argument("--key", default=os.environ.get("LITE3DP_KEY", ""),
                    help="API key (or set LITE3DP_KEY); shown on the printer's "
                         "WiFi Status screen. Only needed for mutating calls.")
    args = ap.parse_args()

    if args.nav is not None:
        navigate(args.host, args.nav, args.key)
        time.sleep(0.3)

    while True:
        try:
            img = fetch_screen(args.host, args.key)
            img.save(args.out)
            print(f"wrote {args.out}", file=sys.stderr)
            if not args.no_show and not args.loop:
                img.show()
        except Exception as e:  # noqa: BLE001 - CLI tool, report and continue
            print(f"error: {e}", file=sys.stderr)
            if not args.loop:
                return 1
        if not args.loop:
            return 0
        time.sleep(args.loop)


if __name__ == "__main__":
    sys.exit(main())
