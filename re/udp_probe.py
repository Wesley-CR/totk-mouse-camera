# Transport probe sender for the ri0 mod (branch experiment/raw-input-bridge).
#
# Sends N UDP datagrams with increasing sequence numbers to 127.0.0.1:51987:
#   struct { uint32 magic; uint32 seq; int32 dx; int32 dy }  (little-endian)
# The mod logs every magic-matching datagram it receives. Run AFTER the game
# has reached the open world (the mod's worker starts ~5 s after the first
# camera frame). No Eden changes, no dependencies.
#
# Usage:  python re/udp_probe.py [count] [interval_ms]

import socket
import struct
import sys
import time

MAGIC = 0x4D4E5657
PORT = 51987
ADDR = "127.0.0.1"


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    interval_ms = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print("udp_probe: sending %d packets to %s:%d" % (count, ADDR, PORT))
    for seq in range(1, count + 1):
        pkt = struct.pack("<IIii", MAGIC, seq, 10 * seq, -5 * seq)
        sock.sendto(pkt, (ADDR, PORT))
        if seq == 1 or seq == count or seq % 10 == 0:
            print("udp_probe: sent seq=%d" % seq)
        time.sleep(interval_ms / 1000.0)
    print("udp_probe: done")


if __name__ == "__main__":
    main()
