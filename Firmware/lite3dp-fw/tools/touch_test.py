#!/usr/bin/env python3
"""Touchscreen bring-up harness — runs the tests from the touch test plan.

    python tools/touch_test.py idle --seconds 60      # Test 1: IRQ noise floor
    python tools/touch_test.py pressure               # Test 2: threshold hunt
    python tools/touch_test.py sweep --seconds 30     # Test 2, hands-free
    python tools/touch_test.py stability --n 200      # Test 3: sample jitter
    python tools/touch_test.py capture                # Test 5: 5-point capture
    python tools/touch_test.py fit                    # fit + apply calibration
    python tools/touch_test.py watch                  # live readout

The device side is GET /api/touch/raw and GET/POST /api/touch/cal. Every
endpoint needs the per-device API key: pass --key or set LITE3DP_KEY.
"""

import argparse
import os
import statistics
import sys
import time

import requests

DEFAULT_HOST = "lite3dp.local"


def auth_headers(key):
    return {"X-Api-Key": key} if key else {}


class Device:
    def __init__(self, host, key):
        self.host = host
        self.key = key

    def raw(self, n=5, retries=3):
        """A read can lose the SPI device to the UI task and 500. Retry rather
        than abandon a long soak over one dropped sample."""
        last = None
        for attempt in range(retries):
            try:
                r = requests.get(f"http://{self.host}/api/touch/raw",
                                 params={"n": n}, headers=auth_headers(self.key),
                                 timeout=10)
                r.raise_for_status()
                return r.json()
            except requests.RequestException as e:
                last = e
                time.sleep(0.1 * (attempt + 1))
        raise last

    def get_cal(self):
        r = requests.get(f"http://{self.host}/api/touch/cal",
                         headers=auth_headers(self.key), timeout=10)
        r.raise_for_status()
        return r.json()

    def post_cal(self, **params):
        r = requests.post(f"http://{self.host}/api/touch/cal",
                          params=params, headers=auth_headers(self.key), timeout=10)
        r.raise_for_status()
        return r.json()


def cmd_idle(dev, args):
    """Test 1 — how often does the IRQ pin claim a touch that isn't there?"""
    print(f"Do not touch the panel. Sampling for {args.seconds}s...")
    end = time.time() + args.seconds
    total = irq_low = gated = 0
    pressures = []

    while time.time() < end:
        s = dev.raw(args.n)
        total += 1
        pressures.append(s["pressure"])
        if s["irq"] == 0:
            irq_low += 1
            if s["pressure"] >= args.pmin:
                gated += 1
        time.sleep(args.interval)

    print(f"\nsamples          {total}")
    print(f"IRQ low          {irq_low} ({100.0 * irq_low / max(total, 1):.1f}%)")
    print(f"  ...and pressure >= {args.pmin}: {gated}  <- these become ghost touches")
    print(f"pressure  max {max(pressures)}  mean {statistics.mean(pressures):.1f}")
    if irq_low and not gated:
        print("\nIRQ floats, but pressure gating rejects every spurious low. Good.")
    elif gated:
        print(f"\n{gated} sample(s) would have reached the UI. Raise pmin above "
              f"{max(p for p in pressures)} and re-run.")
    else:
        print("\nIRQ is stable at idle — the panel module has its own pull-up.")


def cmd_pressure(dev, args):
    """Test 2 — find a threshold separating real contact from noise."""
    stages = [
        ("nothing touching the panel", "untouched"),
        ("a light fingertip", "light"),
        ("firm finger pressure", "firm"),
        ("a stylus tip", "stylus"),
    ]
    results = {}
    for prompt, name in stages:
        input(f"\nHold {prompt}, then press Enter...")
        vals = []
        for _ in range(args.n):
            vals.append(dev.raw(5)["pressure"])
            time.sleep(0.05)
        results[name] = vals
        print(f"  {name:10s} min {min(vals):5d}  med {statistics.median(vals):7.1f}  max {max(vals):5d}")

    floor = max(results["untouched"])
    lightest = min(results["light"] + results["stylus"])
    print(f"\nuntouched ceiling {floor}   lightest real touch {lightest}")
    if lightest > floor:
        suggested = floor + (lightest - floor) // 2
        print(f"clean gap -> suggested pmin = {suggested}")
        print(f"apply with: python tools/touch_test.py fit --pmin {suggested}")
    else:
        print("NO GAP. Pressure alone cannot separate touch from noise — the driver "
              "needs consecutive-agreeing-sample confirmation as well.")


def cmd_sweep(dev, args):
    """Test 2, hands-free — sample continuously while the panel is touched.

    Unlike `pressure`, this needs no keyboard: touch the panel however you like
    during the window and the run is split by IRQ state afterwards.
    """
    print(f"Touch the panel on and off for {args.seconds}s — vary light/firm. Go.")
    end = time.time() + args.seconds
    touched, idle = [], []

    while time.time() < end:
        s = dev.raw(args.n)
        (touched if s["irq"] == 0 else idle).append(s["pressure"])
        time.sleep(args.interval)

    print(f"\nIRQ high (no contact): {len(idle)} samples", end="")
    if idle:
        print(f"   pressure max {max(idle)}  mean {statistics.mean(idle):.1f}")
    else:
        print()

    if not touched:
        print("\nIRQ never went low — nothing registered as a touch at all.")
        return 1

    print(f"IRQ low  (contact):    {len(touched)} samples   "
          f"pressure min {min(touched)}  med {statistics.median(touched):.1f}  max {max(touched)}")

    floor = max(idle) if idle else 0
    lightest = min(touched)
    print(f"\nidle ceiling {floor}   lightest contact {lightest}")
    if lightest > floor:
        suggested = floor + (lightest - floor) // 2
        print(f"clean gap -> suggested pmin = {suggested}  (currently {args.pmin})")
    else:
        overlap = [p for p in touched if p <= floor]
        print(f"OVERLAP: {len(overlap)}/{len(touched)} contact samples sit at or below "
              f"the idle ceiling. Pressure alone cannot gate cleanly.")
    return 0


def cmd_stability(dev, args):
    """Test 3 — jitter at a fixed point, to catch SPI contention."""
    print(f"Hold a stylus still at the center of the screen for the next "
          f"{args.seconds}s. Sampling until {args.n} contact samples land.")
    xs, ys, zs = [], [], []
    deadline = time.time() + args.seconds
    while len(xs) < args.n and time.time() < deadline:
        s = dev.raw(5)
        if s["pressure"] < args.pmin:
            continue
        xs.append(s["x"])
        ys.append(s["y"])
        zs.append(s["pressure"])
        time.sleep(0.01)

    if len(xs) < 10:
        print(f"Only {len(xs)} samples cleared pmin={args.pmin}. Press harder, "
              f"hold longer, or lower pmin.")
        return 1

    for name, v in (("raw X", xs), ("raw Y", ys), ("pressure", zs)):
        print(f"{name:9s} med {statistics.median(v):7.1f}  stdev {statistics.pstdev(v):6.1f}  "
              f"range {min(v)}..{max(v)} ({max(v) - min(v)})")
    print(f"\naccepted {len(xs)}/{args.n} samples")
    print("Re-run this while the UI redraws and during an SD read. If the spread "
          "grows, the shared SPI bus is the problem, not the panel.")


def cmd_capture(dev, args):
    """Test 5 — drive the on-device crosshair capture."""
    dev.post_cal(start=1)
    print("Crosshair capture started on the panel.")
    print("Tap each cross as it appears (5 total), then run: touch_test.py fit")

    seen = 0
    while seen < 5:
        time.sleep(1.0)
        pts = dev.get_cal().get("points", [])
        if len(pts) > seen:
            for p in pts[seen:]:
                print(f"  point {len(pts)}: target({p['target_x']},{p['target_y']}) "
                      f"raw({p['raw_x']},{p['raw_y']}) z={p['pressure']}")
            seen = len(pts)
    print("All five captured.")


def fit_axis(samples):
    """Least-squares fit of screen = a*raw + b, returned as (a, b)."""
    n = len(samples)
    sx = sum(r for r, _ in samples)
    sy = sum(s for _, s in samples)
    sxx = sum(r * r for r, _ in samples)
    sxy = sum(r * s for r, s in samples)
    denom = n * sxx - sx * sx
    if denom == 0:
        raise ValueError("degenerate fit — all raw values identical")
    a = (n * sxy - sx * sy) / denom
    b = (sy - a * sx) / n
    return a, b


def cmd_fit(dev, args):
    """Turn captured points into calibration constants and push them."""
    cal = dev.get_cal()
    pts = cal.get("points", [])
    if len(pts) < 3:
        print(f"Only {len(pts)} captured points. Run 'capture' first.")
        return 1

    w, h = cal["w"], cal["h"]
    swap = bool(cal["swap"])

    # Which raw channel feeds which screen axis, per the live cal.
    x_src = "raw_y" if swap else "raw_x"
    y_src = "raw_x" if swap else "raw_y"

    ax, bx = fit_axis([(p[x_src], p["target_x"]) for p in pts])
    ay, by = fit_axis([(p[y_src], p["target_y"]) for p in pts])

    # Convert slope/intercept into the endpoint form the firmware stores:
    # raw at screen 0 and at screen max, with sign folded into invert_*.
    def endpoints(a, b, screen_max):
        # The fit is extrapolated to the screen edges, which can land outside
        # the ADC's 12-bit range. The firmware stores these as uint16_t and
        # clamps inputs to the range anyway, so clamp here too — it costs about
        # a pixel and keeps the value from wrapping to 65526.
        def clamp(v):
            return max(0, min(4095, int(round(v))))
        raw_at_0 = (0 - b) / a
        raw_at_max = (screen_max - b) / a
        if raw_at_0 <= raw_at_max:
            return clamp(raw_at_0), clamp(raw_at_max), False
        return clamp(raw_at_max), clamp(raw_at_0), True

    xmin, xmax, invx = endpoints(ax, bx, w - 1)
    ymin, ymax, invy = endpoints(ay, by, h - 1)

    resid = []
    for p in pts:
        px = ax * p[x_src] + bx
        py = ay * p[y_src] + by
        resid.append((abs(px - p["target_x"]), abs(py - p["target_y"])))
    worst_x = max(r[0] for r in resid)
    worst_y = max(r[1] for r in resid)

    print(f"fit: x = {ax:.4f}*{x_src} + {bx:.1f}   y = {ay:.4f}*{y_src} + {by:.1f}")
    print(f"worst residual: {worst_x:.1f} px in X, {worst_y:.1f} px in Y")
    if max(worst_x, worst_y) > 10:
        print("  ^ over the 10 px bar. A per-axis linear fit is not enough; the "
              "panel likely needs an affine fit that accounts for skew.")

    params = dict(xmin=xmin, xmax=xmax, ymin=ymin, ymax=ymax,
                  invx=int(invx), invy=int(invy))
    if args.pmin is not None:
        params["pmin"] = args.pmin

    if args.dry_run:
        print(f"\nwould apply: {params}")
        return 0

    print(f"\napplying: {params}")
    print(dev.post_cal(**params))
    print("\nLive only — reflash with these values in touch_input.c to persist.")
    return 0


def cmd_watch(dev, args):
    """Live readout, for eyeballing behaviour while poking the panel."""
    print("Ctrl-C to stop.")
    try:
        while True:
            s = dev.raw(args.n)
            state = "DOWN" if s["pressure"] >= args.pmin else "  up"
            print(f"\r{state}  irq={s['irq']}  raw({s['x']:4d},{s['y']:4d})  "
                  f"spread({s['x_spread']:3d},{s['y_spread']:3d})  z={s['pressure']:4d}  "
                  f"-> ({s['mapped_x']:3d},{s['mapped_y']:3d})   ", end="", flush=True)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--key", default=os.environ.get("LITE3DP_KEY"))
    ap.add_argument("--n", type=int, default=5, help="samples per request")
    ap.add_argument("--pmin", type=int, default=None, help="pressure threshold")
    ap.add_argument("--interval", type=float, default=0.1)
    ap.add_argument("--seconds", type=int, default=60)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("cmd", choices=["idle", "pressure", "sweep", "stability",
                                    "capture", "fit", "watch"])
    args = ap.parse_args()

    dev = Device(args.host, args.key)

    # Default the threshold to whatever the device is actually using.
    if args.pmin is None and args.cmd != "fit":
        try:
            args.pmin = dev.get_cal()["pmin"]
        except Exception as e:
            print(f"could not read live cal ({e}); assuming pmin=100")
            args.pmin = 100

    handlers = {
        "idle": cmd_idle, "pressure": cmd_pressure, "sweep": cmd_sweep,
        "stability": cmd_stability, "capture": cmd_capture, "fit": cmd_fit,
        "watch": cmd_watch,
    }
    if args.cmd in ("idle", "watch"):
        args.n = max(args.n, 3)
    return handlers[args.cmd](dev, args) or 0


if __name__ == "__main__":
    sys.exit(main())
