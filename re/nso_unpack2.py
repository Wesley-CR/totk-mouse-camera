#!/usr/bin/env python3
"""Unpack a Nintendo NSO0 module with the CORRECT header field offsets.

The earlier tools in this directory read the segment table at 12-byte strides
(0x10, 0x1C, 0x28) which is wrong: the layout is

    0x00  magic "NSO0"
    0x04  version            (u32)
    0x08  reserved
    0x0C  flags              (u32)
    0x10  text   { file_off, mem_off, size }   (3 x u32)
    0x1C  rodata { file_off, mem_off, size }
    0x28  data   { file_off, mem_off, size }
    0x34  bss size           (u32)
    0x38  module id          (0x20 bytes)  -> also the build id
    0x58  ...
    0x60  text compressed size    (u32)
    0x64  rodata compressed size  (u32)
    0x68  data compressed size    (u32)

Note that the *first* u32 of each segment entry is the section's memory offset
observed at runtime in some tooling, and rodata/data live at 0x20/0x30 in the
raw header - hence reading at 0x1C/0x28 produced a plausible-looking but wrong
result, and the decompressed image diverged partway through.

Each segment body is a separate LZ4 block, placed in the file at the offset
given by the NEXT segment's file offset (or file end for the last one), with the
compressed length taken from 0x60/0x64/0x68.

Usage:
    python nso_unpack2.py <module> [outdir]
"""
import lz4.block
import os
import struct
import sys

# (name, file_offset_field, mem_offset_field, mem_size_field, comp_size_field)
SEG_FIELDS = [
    ("text",   0x10, 0x14, 0x18, 0x60),
    ("rodata", 0x20, 0x24, 0x28, 0x64),
    ("data",   0x30, 0x34, 0x38, 0x68),
]


def parse(d):
    if d[:4] != b"NSO0":
        raise ValueError("not NSO0 (magic %r)" % d[:4])
    flags, = struct.unpack_from("<I", d, 0xC)
    bss, = struct.unpack_from("<I", d, 0x34)
    build_id = d[0x38:0x58]

    segs = []
    for name, foff_f, moff_f, msz_f, csz_f in SEG_FIELDS:
        foff, = struct.unpack_from("<I", d, foff_f)
        moff, = struct.unpack_from("<I", d, moff_f)
        msz, = struct.unpack_from("<I", d, msz_f)
        try:
            csz, = struct.unpack_from("<I", d, csz_f)
        except struct.error:
            csz = 0
        segs.append({"name": name, "file_offset": foff, "mem_offset": moff,
                     "mem_size": msz, "comp_size": csz})
    return {"flags": flags, "segments": segs, "bss": bss, "build_id": build_id}


def unpack(path, outdir):
    d = open(path, "rb").read()
    info = parse(d)

    print("file      : %s (%d bytes)" % (path, len(d)))
    print("flags     : 0x%X%s" % (info["flags"], " (LZ4)" if info["flags"] & 1 else ""))
    print("build id  : %s" % info["build_id"].hex().upper())
    print("bss       : 0x%X" % info["bss"])
    print()

    # Lowest memory address defines the image base for offset maths.
    mapped_size = max(s["mem_offset"] + s["mem_size"] for s in info["segments"])
    image = bytearray(mapped_size)

    for i, s in enumerate(info["segments"]):
        start = s["file_offset"]
        csz = s["comp_size"]
        blob = d[start:start + csz] if csz else d[start:]
        try:
            out = lz4.block.decompress(blob, uncompressed_size=s["mem_size"])
        except Exception as e:
            print("  %-7s LZ4 FAILED: %s" % (s["name"], e))
            continue

        ok = len(out) == s["mem_size"]
        print("  %-7s file=0x%-8X comp=0x%-8X mem=0x%-8X size=0x%-8X %s"
              % (s["name"], start, csz, s["mem_offset"], s["mem_size"],
                 "OK" if ok else "SIZE MISMATCH"))

        image[s["mem_offset"]:s["mem_offset"] + len(out)] = out

        if outdir:
            os.makedirs(outdir, exist_ok=True)
            open(os.path.join(outdir, s["name"] + ".bin"), "wb").write(out)

    if outdir:
        os.makedirs(outdir, exist_ok=True)
        p = os.path.join(outdir, "image_mapped.bin")
        open(p, "wb").write(image)
        print()
        print("mapped image: %s (%d bytes, VA == file offset)" % (p, len(image)))

    return info


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    outdir = sys.argv[2] if len(sys.argv) > 2 else None
    unpack(sys.argv[1], outdir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
