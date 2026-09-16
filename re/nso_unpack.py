#!/usr/bin/env python3
"""Unpack a standard Nintendo NSO0 module (per-segment LZ4 compression).

Header layout (Switchbrew NSO0):
    0x00 magic "NSO0"
    0x04 version (u32)
    0x08 reserved
    0x0C flags (u32)
    0x10 text   {file_offset(u32), memory_offset(u32), size(memory, u32)}
    0x1C rodata {file_offset(u32), memory_offset(u32), size(memory, u32)}
    0x28 data   {file_offset(u32), memory_offset(u32), size(memory, u32)}
    0x34 bss_size (u32)
    0x38 module_id (0x20 bytes)   <- the build ID

Each segment's *file* extent is implied by the next segment's file_offset (and
the file size for the last one).  If the flags bit 0 is set the segment bodies
are LZ4-block compressed; the trailing 4 bytes of each file extent hold the
compressed size as a u32 (little endian) placed just before the next segment.

Usage:
    python nso_unpack.py <module> <outdir>
"""
import lz4.block
import os
import struct
import sys


def parse(d):
    if d[:4] != b"NSO0":
        raise ValueError("not NSO0 (magic %r)" % d[:4])
    flags, = struct.unpack_from("<I", d, 0xC)
    segs = []
    for i, name in enumerate(("text", "rodata", "data")):
        fo, mo, msz = struct.unpack_from("<III", d, 0x10 + i * 12)
        segs.append({"name": name, "file_offset": fo, "mem_offset": mo, "mem_size": msz})
    bss, = struct.unpack_from("<I", d, 0x34)
    build_id = d[0x38:0x58]
    # derive each segment's compressed file extent
    for i, s in enumerate(segs):
        end = segs[i + 1]["file_offset"] if i + 1 < len(segs) else len(d)
        # the last 4 bytes before the next segment are the compressed size
        s["next_file_offset"] = end
    return {"flags": flags, "segments": segs, "bss": bss, "build_id": build_id}


def unpack(path, outdir):
    d = open(path, "rb").read()
    info = parse(d)
    print("file            : %s (%d bytes)" % (path, len(d)))
    print("flags           : 0x%X%s" % (info["flags"], "  (LZ4 compressed)" if info["flags"] & 1 else ""))
    print("build id        : %s" % info["build_id"].hex().upper())
    print("bss size        : 0x%X" % info["bss"])
    os.makedirs(outdir, exist_ok=True)

    total = 0
    for s in info["segments"]:
        start = s["file_offset"]
        end = s["next_file_offset"]
        body = d[start:end]
        if info["flags"] & 1:
            # trailing u32 = compressed size; the compressed stream precedes it
            csz, = struct.unpack_from("<I", d, end - 4)
            body = d[start:start + csz]
            try:
                out = lz4.block.decompress(body, uncompressed_size=s["mem_size"])
            except Exception as e:
                print("  %-7s LZ4 FAILED: %s" % (s["name"], e))
                continue
        else:
            out = body[:s["mem_size"]]
        if len(out) != s["mem_size"]:
            print("  %-7s size mismatch: got 0x%X want 0x%X" % (s["name"], len(out), s["mem_size"]))
        dest = os.path.join(outdir, s["name"] + ".bin")
        open(dest, "wb").write(out)
        total += len(out)
        print("  %-7s mem=0x%-9X size=0x%-9X -> %s" % (s["name"], s["mem_offset"], len(out), dest))

    # bss placeholder so the address arithmetic is obvious
    print("  bss     mem=0x%-9X size=0x%X" % (
        info["segments"][2]["mem_offset"] + info["segments"][2]["mem_size"], info["bss"]))
    print("total decompressed: 0x%X" % total)
    return info


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    unpack(sys.argv[1], sys.argv[2])
    return 0


if __name__ == "__main__":
    sys.exit(main())
