#!/usr/bin/env python3
"""Validate the pattern-matching algorithm used by mod/source/nativemouse/scanner.cpp.

The C++ scanner cannot be compiled in this workspace (no devkitA64 yet), so the
algorithm is mirrored here and exercised against known cases.
"""


def parse(pattern):
    out = []
    i, n = 0, len(pattern)
    while i < n:
        while i < n and pattern[i] == ' ':
            i += 1
        if i >= n:
            break
        value = mask = 0
        # high nibble
        if pattern[i] == '?':
            i += 1
        else:
            value |= int(pattern[i], 16) << 4
            mask |= 0xF0
            i += 1
        # low nibble
        if i < n and pattern[i] == '?':
            i += 1
        elif i < n and pattern[i] != ' ':
            value |= int(pattern[i], 16)
            mask |= 0x0F
            i += 1
        out.append((value, mask))
    return out


def find(data, pattern, unique=True):
    p = parse(pattern)
    if not p:
        return None
    found = None
    for s in range(0, len(data) - len(p) + 1):
        if all((data[s + k] & m) == (v & m) for k, (v, m) in enumerate(p)):
            if not unique:
                return s
            if found is not None:
                return None          # ambiguous -> refuse
            found = s
    return found


DATA = bytes.fromhex('1F2003D5AABBCCDD1F2003D5EEFF0011')

CASES = [
    ('parse "1F 20 03 D5"',        parse('1F 20 03 D5'),                [(0x1F, 0xFF), (0x20, 0xFF), (0x03, 0xFF), (0xD5, 0xFF)]),
    ('parse "1F 20 03 D5 ?? ??"',  parse('1F 20 03 D5 ?? ??'),          [(0x1F, 0xFF), (0x20, 0xFF), (0x03, 0xFF), (0xD5, 0xFF), (0x00, 0x00), (0x00, 0x00)]),
    ('parse "1? 2? 03 D5"',        parse('1? 2? 03 D5'),                [(0x10, 0xF0), (0x20, 0xF0), (0x03, 0xFF), (0xD5, 0xFF)]),
    ('unique 1F2003D5 (x2)',       find(DATA, '1F 20 03 D5', True),     None),
    ('first  1F2003D5',            find(DATA, '1F 20 03 D5', False),    0),
    ('unique AABBCCDD',            find(DATA, 'AA BB CC DD', True),     4),
    # DATA is 1F2003D5 AABBCCDD 1F2003D5 EEFF0011, so this really is at 8.
    ('unique 1F2003D5EEFF',        find(DATA, '1F 20 03 D5 EE FF', True), 8),
    # ...and the genuinely absent one must come back None.
    ('absent pattern',             find(DATA, '99 88 77 66', True),     None),
    ('wildcard 1? 2? 03 D5 (x2)',  find(DATA, '1? 2? 03 D5', True),     None),
    ('wildcard A? BB CC DD',       find(DATA, 'A? BB CC DD', True),     4),
    # A '?' byte must match anything. DATA = 1F2003D5 AABBCCDD 1F2003D5 EEFF0011
    # so "?? EE FF" matches at offset 11 (EE FF are the last two bytes of 0D..14).
    ('wildcard ?? EE FF',          find(DATA, '?? EE FF', True),        11),
]

failed = 0
for name, got, want in CASES:
    ok = got == want
    failed += 0 if ok else 1
    print('%-28s %-5s got=%s want=%s' % (name, 'PASS' if ok else 'FAIL', got, want))

print()
print('all passed' if failed == 0 else '%d FAILED' % failed)
raise SystemExit(1 if failed else 0)
