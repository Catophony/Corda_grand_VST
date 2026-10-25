#!/usr/bin/env python3
"""Generate the demo MIDI: dry staccato, chord release, pedal and pedalless arpeggios."""
import struct

PPQ = 480
BPM = 120
TPS = PPQ * BPM / 60.0
events = []

def tick(s): return int(round(s * TPS))
def note(start, dur, pitch, vel):
    events.append((tick(start), 2, bytes([0x90, pitch, vel])))
    events.append((tick(start + dur), 1, bytes([0x80, pitch, max(20, vel - 12)])))
def pedal(s, value): events.append((tick(s), 0, bytes([0xB0, 64, value])))

# Dry staccato: release must be audible, no pedal.
for t, p, v in [(0.30, 60, 100), (0.56, 64, 92), (0.82, 67, 96), (1.08, 72, 104),
                (1.34, 67, 88), (1.60, 64, 84), (1.86, 60, 78)]:
    note(t, 0.18, p, v)

# Held chord with a natural release, still no pedal.
for p, v in [(48, 100), (55, 84), (60, 92), (64, 80)]:
    note(2.25, 2.15, p, v)

# Pedalled arpeggio.
pedal(4.90, 127)
for i, p in enumerate([48, 55, 60, 64, 67, 72, 76, 79, 84]):
    note(5.00 + i * 0.28, 1.3, p, 72 + (i % 3) * 5)
pedal(8.20, 0)

# The same figure dry and staccato.
for i, p in enumerate([48, 55, 60, 64, 67, 72, 76, 79, 84]):
    note(8.85 + i * 0.16, 0.16, p, 76 + (i % 3) * 5)

# Low chord with pedal, then release.
pedal(11.40, 127)
for p, v in [(36, 112), (43, 92), (48, 86)]:
    note(11.55, 2.4, p, v)
pedal(14.45, 0)

# Dry repeated notes.
for i in range(8):
    note(15.00 + i * 0.20, 0.14, 60, 72 + (i % 4) * 6)

# Final chord with pedal, plus quiet high octave.
pedal(17.40, 127)
for p, v in [(36, 120), (43, 96), (48, 92), (52, 84), (55, 80), (60, 90), (64, 82), (67, 78)]:
    note(17.55, 4.4, p, v)
note(18.05, 2.8, 84, 62)
note(18.05, 2.8, 91, 50)
pedal(22.40, 0)

events.sort(key=lambda x: (x[0], x[1]))
data = bytearray(); last = 0

def vlq(n):
    out = [n & 0x7F]; n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(out))

for t, _, payload in events:
    data += vlq(t - last) + payload; last = t
data += b"\x00\xff\x2f\x00"
tempo = struct.pack(">I", int(60_000_000 / BPM))[1:]
track = b"\x00\xff\x51\x03" + tempo + data
smf = b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ) + b"MTrk" + struct.pack(">I", len(track)) + track
open("demo/corda_demo.mid", "wb").write(smf)
print(f"wrote demo/corda_demo.mid ({len(smf)} bytes, {last/TPS:.1f} s)")
