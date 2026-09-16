#!/usr/bin/env python3
"""Full NSO (compressed variant) unpacker + address-space helper.

Layout recovered empirically for the TOTK Optimizer / UltraCam `subsdk3`:

    NSO header  (0x100 bytes, file offsets in the header are NOT usable)
    LZ4 block stream -> [ text | rodata | data ]  concatenated

The text/rodata/data *sizes* and the text/rodata load offsets from the header
ARE usable, and the three segments are contiguous in memory:

    text    mem 0x00000000 .. size 0x168EA0
    rodata  mem 0x00168EA0 .. size filesize(1)-0x1?  (see below)
    data    mem 0x001D2200 .. size 0x12D5FD

Historically for NSO the file offset of the first segment is 0x100 and each
subsequent segment starts at align_up(prev_end, 0x1000), but this build stored
one concatenated compressed blob, so we reconstruct from the decompressor.

Usage:
    python nsoimg.py unpack <module.bin> <outdir>
    python nsoimg.py info   <module.bin>
"""
import lz4.block
import os
import struct
import sys

ALIGN = 0x1000


def parse_header(data):
    version, = struct.unpack_from("<I", data, 4)
    flags, = struct.unpack_from("<I", data, 0xC)
    segs = []
    for i, name in enumerate(("text", "rodata", "data")):
        foff, moff, size = struct.unpack_from("<III", data, 0x10 + i * 12)
        segs.append({"name": name, "file_offset": foff, "mem_offset": moff,
                     "size": size})
    bss, = struct.unpack_from("<I", data, 0x34)
    modid = data[0x38:0x58]
    return {"version": version, "flags": flags, "segments": segs,
            "bss": bss, "module_id": modid, "size": len(data)}


def decompress(path):
    data = open(path, "rb").read()
    info = parse_header(data)
    # Ask for a generous upper bound; lz4 stops when the stream ends.
    cap = sum(s["size"] for s in info["segments"])
    img = lz4.block.decompress(data[0x100:], uncompressed_size=cap)
    return info, img, data


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    cmd, path = sys.argv[1], sys.argv[2]

    if cmd == "info":
        info, img, data = decompress(path)
        print("file size        : %d" % len(data))
        print("decompressed size: %d (0x%X)" % (len(img), len(img)))
        print("module id        : %s" % info["module_id"].hex())
        print("bss size         : 0x%X" % info["bss"])
        print("declared segments:")
        for s in info["segments"]:
            print("  %-6s file=0x%08X mem=0x%08X size=0x%08X"
                  % (s["name"], s["file_offset"], s["mem_offset"], s["size"]))
        idx = img.find(b"MOD0")
        print("MOD0 at image off 0x%X" % idx)
        return 0

    if cmd == "unpack":
        outdir = sys.argv[3] if len(sys.argv) > 3 else "uc_img"
        info, img, data = decompress(path)
        os.makedirs(outdir, exist_ok=True)
        sizes = [s["size"] for s in info["segments"]]
        pos = 0
        print("decompressed %d bytes; extracting" % len(img))
        bases = {}
        for s in info["segments"]:
            end = min(pos + s["size"], len(img))
            blob = img[pos:end]
            out = os.path.join(outdir, s["name"] + ".bin")
            open(out, "wb").write(blob)
            bases[s["name"]] = s["mem_offset"]
            print("  %-7s off=0x%08X size=0x%08X -> %s"
                  % (s["name"], pos, len(blob), out))
            pos = end
        open(os.path.join(outdir, "image.bin"), "wb").write(img)
        # Record bases so other tools agree with us.
        with open(os.path.join(outdir, "bases.txt"), "w") as f:
            for k, v in bases.items():
                f.write("%s 0x%X\n" % (k, v))
        print("  image.bin written (%d bytes)" % len(img))
        print("segment load bases: %s" % bases)
        return 0

    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())
