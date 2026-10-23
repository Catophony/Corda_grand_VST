#!/usr/bin/env python3
"""Generate the bundled MIDI demo (also used to render demo audio via Reaper)."""
import struct

PPQ = 480
BPM = 120
TICKS_PER_SEC = PPQ * BPM / 60.0

events = []  # (tick, order, bytes)

def t(seconds):
    return int(round(seconds * TICKS_PER_SEC))

def on(seconds, note, velocity):
    events.append((t(seconds), 2, bytes([0x90, note, velocity])))

def off(seconds, note, velocity=64):
    events.append((t(seconds), 1, bytes([0x80, note, velocity])))

def pedal(seconds, value):
    events.append((t(seconds), 0, bytes([0xB0, 64, value])))

def note(start, dur, pitch, velocity):
    on(start, pitch, velocity)
    off(start + dur, pitch, max(20, velocity - 15))

# Opening: single notes, then a warm C-major arpeggio.
note(0.30, 2.2, 60, 82)
note(2.55, 2.2, 67, 74)
note(4.70, 2.8, 72, 80)
pedal(4.55, 127)
note(4.75, 2.8, 76, 70)
note(4.75, 2.8, 79, 64)
pedal(7.85, 0)

# Low register and body of the instrument.
pedal(8.1, 127)
for p, v in [(36, 92), (43, 77), (48, 72), (55, 67)]:
    note(8.2, 4.2, p, v)
pedal(12.9, 0)

# Arpeggio with pedal.
pedal(13.3, 127)
arp = [48, 55, 60, 64, 67, 72, 76, 79, 84]
for i, p in enumerate(arp):
    note(13.5 + i * 0.105, 1.4, p, 55 + (i % 4) * 7)
pedal(17.2, 0)

# A minor -> F major -> G major.
for start, pitches, vel in [
    (17.7, [45, 52, 57, 60, 64, 69], 74),
    (21.2, [41, 48, 53, 57, 60, 65], 80),
    (24.7, [43, 50, 55, 59, 62, 67], 78),
]:
    pedal(start - 0.1, 127)
    for i, p in enumerate(pitches):
        note(start + i * 0.035, 2.8, p, vel - i * 2)
    pedal(start + 3.3, 0)

# Final C-major chord, plus a quiet high octave.
pedal(28.0, 127)
for i, (p, v) in enumerate([(36, 96), (43, 82), (48, 78), (52, 74), (55, 70), (60, 76), (64, 68), (67, 64)]):
    note(28.2 + i * 0.025, 5.2, p, v)
note(31.6, 3.0, 84, 55)
note(31.6, 3.0, 91, 47)
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

for tick, _, payload in events:
    data += vlq(tick - last) + payload
    last = tick
data += b"\x00\xff\x2f\x00"

tempo = struct.pack(">I", int(60_000_000 / BPM))[1:]
track = (b"\x00\xff\x51\x03" + tempo + data)
smf = b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ) + b"MTrk" + struct.pack(">I", len(track)) + track
with open("demo/corda_demo.mid", "wb") as f:
    f.write(smf)
print(f"wrote demo/corda_demo.mid ({len(smf)} bytes, {last / TICKS_PER_SEC:.1f} s)")
