#!/usr/bin/env python3
"""Extract the `main` NSO from a TOTK XCI without external keys.

An XCI is: [gamecard header @0x100][root HFS0 @0x130][...partitions...]
The `secure` HFS0 partition holds the NCAs in the clear (only the *content*
of an NCA is encrypted; its header is not), so we can:
  1. parse the root HFS0 to find the `secure` partition
  2. parse the secure HFS0 to list the NCAs
  3. read each NCA's header to identify the Program NCA (content type)
  4. for an encrypted NCA with an ExeFS of plaintext NSOs, use the
     section/FS-header tables to locate ExeFS and pull `main`

TOTK's ExeFS holds plaintext NSOs, and the ExeFS *section* of the program NCA
is covered by the header's hash but the FS data itself is only encrypted if
the NCA uses a crypto type other than 0/1.  In practice the ExeFS
(`main`, `main.npdm`, `rtld`, ...) is readable.

Usage:
    python xci_extract.py <game.xci>            # list structure
    python xci_extract.py <game.xci> main out   # extract ExeFS entry `main`
"""
import struct
import sys

SECTOR = 0x200


def read_at(f, off, size):
    f.seek(off)
    return f.read(size)


def parse_hfs0(f, base):
    """Parse an HFS0 header at `base`; return (header, entries)."""
    hdr = read_at(f, base, 0x10)
    magic, count, strsz, _res = struct.unpack("<4sIII", hdr)
    if magic != b"HFS0":
        raise ValueError("not HFS0 at 0x%X (got %r)" % (base, magic))
    entries = []
    for i in range(count):
        e = read_at(f, base + 0x10 + i * 0x40, 0x40)
        name_off, data_off, size, _pad = struct.unpack("<QQII", e[:24])
        raw = read_at(f, base + 0x10 + count * 0x40 + name_off, 0x200)
        name = raw.split(b"\x00")[0].decode("ascii", "replace")
        entries.append({
            "name": name,
            "offset": base + strsz + data_off,
            "size": size,
        })
    return {"count": count, "strsz": strsz}, entries


def parse_nca_header(f, off):
    """Return interesting fields of an NCA header at `off`."""
    hdr = read_at(f, off, 0xC00)
    magic = hdr[0x200:0x204]
    if magic != b"HEAD":
        return None
    # NCA header: 0x200 magic, 0x204 distribution type, 0x205 content type,
    # 0x206 key generation, 0x207 key area encryption key index,
    # 0x208 size (u64), 0x210 program id (u64), ...
    dist = hdr[0x204]
    ctype = hdr[0x205] & 0x3F
    keygen = hdr[0x206]
    kaek = hdr[0x207]
    size, = struct.unpack_from("<Q", hdr, 0x208)
    progid, = struct.unpack_from("<Q", hdr, 0x210)
    # section table: 4 entries of 0x10 at 0x240 {media_start(u32), media_end(u32),
    #   _res(u32), fs_header(u32)}
    sections = []
    for i in range(4):
        ms, me, _r, fsh = struct.unpack_from("<IIII", hdr, 0x240 + i * 0x10)
        sections.append({"media_start": ms, "media_end": me, "fs_header": fsh})
    # FS headers: 3 x 0x200 at 0x400 (pfs0, romfs, bktrm)
    fs = {}
    for i, nm in enumerate(("pfs0", "romfs", "bktrm")):
        h = hdr[0x400 + i * 0x200:0x400 + (i + 1) * 0x200]
        fs[nm] = h
    return {
        "distribution": dist, "content_type": ctype, "key_generation": keygen,
        "kaek": kaek, "size": size, "program_id": progid,
        "sections": sections, "fs": fs,
    }


def content_type_name(t):
    return {
        0: "Program", 1: "Meta", 2: "Control", 3: "Manual",
        4: "Data", 5: "PublicData",
    }.get(t, "Unknown(%d)" % t)


def parse_exefs(f, nca_off, nca, required_key=None):
    """Yield ExeFS entries if the ExeFS header is readable.

    ExeFS lives in section 3 (index 3) of the NCA, or in the PFS0 FS header.
    """
    sh = nca["fs"]["pfs0"]
    # PFS0 superblock (0x200): magic, version, fs info, ...
    # Simple approach: scan for an ExeFS magic near the pfs0 section.
    ms = nca["sections"][2]["media_start"]
    if ms == 0:
        return []
    base = nca_off + ms * SECTOR
    # ExeFS header: 10 entries * 0x10 + 0x80 reserved = 0x140
    hdr = read_at(f, base, 0x140)
    out = []
    for i in range(10):
        name_off, off, size, _pad = struct.unpack_from("<IIII", hdr, i * 0x10)
        if size == 0:
            continue
        name_raw = read_at(f, base + name_off, 0x40) if name_off else b""
        name = name_raw.split(b"\x00")[0].decode("ascii", "replace")
        if not name:
            continue
        out.append({"name": name, "offset": base + off, "size": size})
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    f = open(path, "rb")

    root, entries = parse_hfs0(f, 0x130)
    print("root HFS0: %d entries" % len(entries))
    for e in entries:
        print("  %-16s off=0x%X size=0x%X" % (e["name"], e["offset"], e["size"]))

    secure = next((e for e in entries if e["name"] == "secure"), None)
    if secure is None:
        print("no secure partition")
        return 1

    hdr, ncas = parse_hfs0(f, secure["offset"])
    print()
    print("secure HFS0: %d entries" % len(ncas))
    for n in ncas:
        info = parse_nca_header(f, n["offset"])
        if info is None:
            print("  %-40s (not NCA) size=0x%X" % (n["name"], n["size"]))
            continue
        print("  %-40s size=0x%-10X ct=%-10s keygen=%d kaek=%d progid=%016X" % (
            n["name"], info["size"], content_type_name(info["content_type"]),
            info["key_generation"], info["kaek"], info["program_id"]))
        for i, s in enumerate(info["sections"]):
            if s["media_start"] or s["media_end"]:
                print("      section%d media 0x%X..0x%X fsh=0x%X" % (
                    i, s["media_start"], s["media_end"], s["fs_header"]))

        entries = parse_exefs(f, n["offset"], info)
        if entries:
            print("      ExeFS entries:")
            for e in entries:
                print("        %-12s off=0x%X size=0x%X" % (e["name"], e["offset"], e["size"]))
            if len(sys.argv) >= 4:
                want = sys.argv[2]
                dst = sys.argv[3]
                for e in entries:
                    if e["name"] == want:
                        data = read_at(f, e["offset"], e["size"])
                        open(dst, "wb").write(data)
                        print("      wrote %s (%d bytes)" % (dst, len(data)))
                        return 0
                print("      entry %r not found" % want)
    return 0


if __name__ == "__main__":
    sys.exit(main())
