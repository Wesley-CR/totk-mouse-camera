#!/usr/bin/env python3
"""Minimal Nintendo Switch NSO parser / segment extractor.

Usage:
    python nsotool.py info  <module.nso>
    python nsotool.py split <module.nso> <outdir>
"""
import struct
import sys
import os


def parse(data):
    if data[:4] != b"NSO0":
        raise ValueError("not an NSO (magic %r)" % data[:4])
    # NSO header layout (verified against real modules):
    #   0x00 magic "NSO0"   0x04 version   0x08 reserved
    #   0x0C flags
    #   0x10 text   {file_off, mem_off, size}
    #   0x1C rodata {file_off, mem_off, size}
    #   0x28 data   {file_off, mem_off, size}
    #   0x34 bss_size
    #   0x38 module_id[0x20]
    version, = struct.unpack_from("<I", data, 4)
    flags, = struct.unpack_from("<I", data, 0xC)
    segs = []
    for i, name in enumerate(("text", "rodata", "data")):
        foff, moff, size = struct.unpack_from("<III", data, 0x10 + i * 12)
        segs.append({"name": name, "file_offset": foff, "mem_offset": moff, "size": size})
    bss, = struct.unpack_from("<I", data, 0x34)
    modid = data[0x38:0x38 + 0x20]
    return {
        "version": version,
        "flags": flags,
        "segments": segs,
        "bss_size": bss,
        "module_id": modid,
        "total": len(data),
    }


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    cmd, path = sys.argv[1], sys.argv[2]
    data = open(path, "rb").read()
    info = parse(data)

    if cmd == "info":
        print("file        : %s (%d bytes)" % (path, info["total"]))
        print("version     : %d" % info["version"])
        print("flags       : 0x%08X" % info["flags"])
        print("module id   : %s" % info["module_id"].hex())
        print("bss size    : 0x%X" % info["bss_size"])
        for s in info["segments"]:
            end = s["file_offset"] + s["size"]
            over = " OVERRUNS-FILE" if end > info["total"] else ""
            print("  %-7s file=0x%08X mem=0x%08X size=0x%08X end=0x%08X%s"
                  % (s["name"], s["file_offset"], s["mem_offset"], s["size"], end, over))
        return 0

    if cmd == "split":
        outdir = sys.argv[3]
        os.makedirs(outdir, exist_ok=True)
        for s in info["segments"]:
            blob = data[s["file_offset"]:s["file_offset"] + s["size"]]
            out = os.path.join(outdir, s["name"] + ".bin")
            open(out, "wb").write(blob)
            print("%-7s -> %s (0x%X bytes) mem_base=0x%X"
                  % (s["name"], out, len(blob), s["mem_offset"]))
        return 0

    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())
