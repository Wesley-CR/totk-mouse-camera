#!/usr/bin/env python3
"""Scan the unpacked UltraCam image for TOTK `main`-module code offsets.

UltraCam is exlaunch-based and therefore carries per-game-version hardcoded
offsets (there is no pattern scanning in exlaunch).  Those offsets take the
form of small integers (< 0x4000000) stored in `.rodata` tables, and the
runtime VA of a `main`-module symbol is computed as

    main_base + offset

Two encodings are possible:
  (a) raw offset            : 0x00000000 .. 0x04000000
  (b) runtime absolute VA   : 0x7100000000 + offset   (Switch app main base)
  (c) module-relative VA    : offset (same as (a))

We look for runs/tables of such values, which is how exlaunch `UserTableType`
tables look, and print their file/VA locations so they can be correlated with
nearby strings.

Usage:
    python find_offsets.py [min_run]
"""
import struct
import sys

IMAGE = "uc_img/image.bin"
TEXT_END = 0x168EA0          # text lives [0, TEXT_END)
RODATA_END = 0x1EEBD1
TOTK_MAIN_MAX = 0x04000000   # plausible upper bound for a main-module offset
SWITCH_MAIN_BASE = 0x7100000000


def looks_like_offset(v):
    # executable, page-ish alignment not required, inside main module
    return 0x1000 <= v < TOTK_MAIN_MAX and (v & 3) == 0


def main():
    data = open(IMAGE, "rb").read()
    min_run = int(sys.argv[1]) if len(sys.argv) > 1 else 3

    # --- pass 1: runs of consecutive u64 offsets -------------------------
    print("=== u64 runs of plausible main-module offsets (>= %d) ===" % min_run)
    run_start = None
    run_vals = []
    for off in range(RODATA_END - 8, TEXT_END - 1, -8):
        pass  # placeholder to keep loop style explicit

    off = TEXT_END
    while off + 8 <= RODATA_END:
        v, = struct.unpack_from("<Q", data, off)
        if looks_like_offset(v):
            if run_start is None:
                run_start = off
            run_vals.append(v)
        else:
            if run_start is not None and len(run_vals) >= min_run:
                print("  VA 0x%08X  %d entries: %s" % (
                    run_start, len(run_vals),
                    " ".join("0x%X" % x for x in run_vals[:14])))
            run_start, run_vals = None, []
        off += 8
    if run_start is not None and len(run_vals) >= min_run:
        print("  VA 0x%08X  %d entries: %s" % (
            run_start, len(run_vals), " ".join("0x%X" % x for x in run_vals[:14])))

    # --- pass 2: u64 absolute runtime VAs --------------------------------
    print()
    print("=== u64 values that look like 0x7100_0000_0000 + offset ===")
    off = TEXT_END
    seen = 0
    while off + 8 <= RODATA_END:
        v, = struct.unpack_from("<Q", data, off)
        if SWITCH_MAIN_BASE <= v < SWITCH_MAIN_BASE + 0x08000000:
            print("  VA 0x%08X -> 0x%X  (offset 0x%X)" % (off, v, v - SWITCH_MAIN_BASE))
            seen += 1
            if seen > 60:
                print("  ... (truncated)")
                break
        off += 8
    if seen == 0:
        print("  (none)")

    # --- pass 3: u32 pairs (offset, type) as used by some tables ---------
    print()
    print("=== u32 runs of plausible offsets (>= %d) ===" % min_run)
    off = TEXT_END
    run_start, run_vals = None, []
    while off + 4 <= RODATA_END:
        v, = struct.unpack_from("<I", data, off)
        if looks_like_offset(v):
            if run_start is None:
                run_start = off
            run_vals.append(v)
        else:
            if run_start is not None and len(run_vals) >= min_run:
                print("  VA 0x%08X  %d entries: %s" % (
                    run_start, len(run_vals),
                    " ".join("0x%X" % x for x in run_vals[:16])))
            run_start, run_vals = None, []
        off += 4
    return 0


if __name__ == "__main__":
    sys.exit(main())
