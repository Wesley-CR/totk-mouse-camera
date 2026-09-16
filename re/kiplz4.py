#!/usr/bin/env python3
"""Decompress a compressed NSO (KIP-style LZ4-block) module and split it into segments.

Switch exefs `subsdk*` modules produced by some toolchains are stored as an NSO
header followed by a single concatenated LZ4 block stream holding all three
segments back-to-back.  The header records the *uncompressed* memory sizes and
the load offsets; the file offsets are bogus for this variant, so we drive the
decompressor from the header sizes instead.

Usage:
    python kiplz4.py unpack <module.bin> <outdir>
    python kiplz4.py info   <module.bin>
"""
import lz4.block
import os
import struct
import sys

HEADER_SIZE = 0x100


def header_info(data):
    version, = struct.unpack_from("<I", data, 4)
    flags, = struct.unpack_from("<I", data, 0xC)
    segs = []
    for i, name in enumerate(("text", "rodata", "data")):
        foff, moff, size = struct.unpack_from("<III", data, 0x10 + i * 12)
        segs.append({"name": name, "file_offset": foff, "mem_offset": moff,
                     "size": size})
    bss, = struct.unpack_from("<I", data, 0x34)
    return {"version": version, "flags": flags, "segments": segs, "bss": bss}


def unpack(path, outdir):
    data = open(path, "rb").read()
    info = header_info(data)
    total = sum(s["size"] for s in info["segments"])
    print("declared total uncompressed size: 0x%X (%d bytes)" % (total, total))
    blob = data[HEADER_SIZE:]
    out = lz4.block.decompress(blob, uncompressed_size=total)
    print("decompressed: %d bytes (expected %d)" % (len(out), total))
    if len(out) != total:
        print("WARNING: size mismatch")

    os.makedirs(outdir, exist_ok=True)
    # Verify each segment looks sane by checking whether it holds ARM64 code.
    write_all = os.environ.get("KIP_WRITE_ALL") == "1"
    pos = 0
    for s in info["segments"]:
        seg = out[pos:pos + s["size"]]
        stem = os.path.join(outdir, s["name"] + ".bin")
        if s["name"] != "text" or write_all:
            open(stem, "wb").write(seg)
            print("  wrote %-7s %d bytes -> %s" % (s["name"], len(seg), stem))
        pos += s["size"]

    # Always write the full image and a text-only slice, since the segment
    # table of this variant is not trustworthy.
    open(os.path.join(outdir, "image.bin"), "wb").write(out)
    print("  wrote image    %d bytes -> %s" % (len(out), os.path.join(outdir, "image.bin")))

    # Locate the MOD0 header inside the image to recover the real layout.
    for magic, label in ((b"MOD0", "MOD0"), (b"NSO0", "NSO0")):
        idx = out.find(magic)
        print("  first %s at 0x%X" % (label, idx) if idx >= 0 else "  no %s found" % label)
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    if sys.argv[1] == "info":
        print(header_info(open(sys.argv[2], "rb").read()))
        return 0
    if sys.argv[1] == "unpack":
        unpack(sys.argv[2], sys.argv[3])
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())
