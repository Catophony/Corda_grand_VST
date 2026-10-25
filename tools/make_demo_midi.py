#!/usr/bin/env python3
"""Generate the bundled MIDI demo (the same performance as the offline demo)."""
import struct

PPQ = 480
BPM = 120
TICKS_PER_SEC = PPQ * BPM / 60.0

events = []

def tick(seconds):
    return int(round(seconds * TICKS_PER_SEC))

def note(start, dur, pitch, velocity):
    events.append((tick(start), 2, bytes([0x90, pitch, velocity])))
    events.append((tick(start + dur), 1, bytes([0x80, pitch, max(20, velocity - 15)])))

def pedal(seconds, value):
    events.append((tick(seconds), 0, bytes([0xB0, 64, value])))

# Opening single notes.
note(0.35, 2.8, 60, 105)
note(3.35, 2.7, 67, 92)

# Warm mid chord.
for p, v in [(48, 115), (55, 92), (64, 83)]:
    note(6.20, 3.4, p, v)

# Low register.
for p, v in [(21, 121), (33, 96), (45, 79), (57, 70)]:
    note(9.90, 4.0, p, v)

# Pedalled arpeggio.
pedal(16.0, 127)
arp = [48, 55, 60, 64, 67, 72, 76, 79]
for i in range(20):
    note(16.1 + i * 0.115, 1.0, arp[i % 8] + (12 if i >= 8 else 0),
         70 + (i % 4) * 2)
pedal(20.2, 0)

# Short low chord.
for p, v in [(36, 108), (48, 89), (55, 79)]:
    note(20.3, 3.5, p, v)

# Fast run.
pedal(24.0, 127)
run = [72, 74, 76, 77, 79, 81, 83, 84]
for i in range(8):
    note(24.2 + i * 0.085, 0.7, run[i], 41 + i * 8)
pedal(25.6, 0)

# Final C-major spread, plus quiet high octave.
pedal(25.6, 127)
for i, (p, v) in enumerate([(24, 127), (36, 99), (48, 89), (60, 92), (64, 83), (67, 79)]):
    note(25.6 + i * 0.0, 5.0, p, v)
note(26.1, 3.0, 84, 70)
note(26.1, 3.0, 91, 57)
pedal(34.6, 0)

events.sort(key=lambda x: (x[0], x[1]))
data = bytearray()
last = 0

def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))

for t, _, payload in events:
    data += vlq(t - last) + payload
    last = t
data += b"\x00\xff\x2f\x00"

tempo = struct.pack(">I", int(60_000_000 / BPM))[1:]
track = b"\x00\xff\x51\x03" + tempo + data
smf = b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ) + b"MTrk" + struct.pack(">I", len(track)) + track
with open("demo/corda_demo.mid", "wb") as f:
    f.write(smf)
print(f"wrote demo/corda_demo.mid ({len(smf)} bytes, {last / TICKS_PER_SEC:.1f} s)")
