#!/usr/bin/env python3
"""Generate sound_data.h from openpilot WAV files for the STM32H7 12-bit DAC."""

import os
import random
import struct
import sys


AMPLITUDE = 0.40
FADE_FRACTION = 0.10

SOUNDS = [
  ("engage", "engage.wav"),
  ("disengage", "disengage.wav"),
  ("prompt", "prompt.wav"),
  ("refuse", "refuse.wav"),
  ("warning_soft", "warning_soft.wav"),
  ("warning_imm", "warning_immediate.wav"),
]


def read_wav_mono_16bit(path):
  with open(path, "rb") as f:
    riff = f.read(12)
    assert riff[:4] == b"RIFF" and riff[8:12] == b"WAVE"

    while True:
      chunk_hdr = f.read(8)
      if len(chunk_hdr) < 8:
        break

      chunk_id = chunk_hdr[:4]
      chunk_size = struct.unpack("<I", chunk_hdr[4:8])[0]
      if chunk_id == b"fmt ":
        fmt = f.read(chunk_size)
        audio_fmt = struct.unpack("<H", fmt[0:2])[0]
        channels = struct.unpack("<H", fmt[2:4])[0]
        sample_rate = struct.unpack("<I", fmt[4:8])[0]
        bits = struct.unpack("<H", fmt[14:16])[0]
        assert audio_fmt == 1 and channels == 1 and bits == 16, \
          f"{path}: expected PCM mono 16-bit, got fmt={audio_fmt} ch={channels} bits={bits}"
        assert sample_rate == 48000, f"{path}: expected 48kHz, got {sample_rate}"
      elif chunk_id == b"data":
        raw = f.read(chunk_size)
        return list(struct.unpack(f"<{len(raw) // 2}h", raw))
      else:
        f.read(chunk_size)

  raise ValueError(f"{path}: no data chunk")


def convert(samples):
  random.seed(42)
  fade_start = int(len(samples) * (1.0 - FADE_FRACTION))
  out = []

  for i, sample in enumerate(samples):
    value = sample / 32768.0 * AMPLITUDE
    if i >= fade_start:
      value *= 1.0 - (i - fade_start) / (len(samples) - fade_start)

    dither = (random.random() - 0.5) + (random.random() - 0.5)
    dac = int(value * 2048.0 + 2048.0 + dither)
    out.append(max(0, min(4095, dac)))

  return out


def main():
  if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <sounds_dir> <output.h>", file=sys.stderr)
    sys.exit(1)

  sounds_dir, output = sys.argv[1], sys.argv[2]
  lines = [
    "#pragma once",
    f"// Auto-generated from openpilot WAVs (48kHz, 12-bit, {int(AMPLITUDE * 100)}% amplitude, {int(FADE_FRACTION * 100)}% fade-out)",
    "",
  ]

  names = []
  for name, wav_file in SOUNDS:
    samples = read_wav_mono_16bit(os.path.join(sounds_dir, wav_file))
    dac_values = convert(samples)
    names.append((name, len(dac_values)))

    lines.append(f"static const uint16_t sound_{name}[{len(dac_values)}] = {{")
    for i in range(0, len(dac_values), 16):
      lines.append("  " + ",".join(str(v) for v in dac_values[i:i + 16]) + ",")
    lines.append("};")
    lines.append("")

  lines.append("typedef struct { const uint16_t *data; uint32_t len; } sound_entry_t;")
  lines.append("static const sound_entry_t op_sounds[] = {")
  lines.append("  {NULL, 0},")
  for name, length in names:
    lines.append(f"  {{sound_{name}, {length}U}},")
  lines.append("};")
  lines.append(f"#define OP_SOUNDS_COUNT {len(names) + 1}")
  lines.append("")

  os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
  with open(output, "w") as f:
    f.write("\n".join(lines))

  total_bytes = sum(length * 2 for _, length in names)
  print(f"Generated {output}: {len(names)} sounds, {total_bytes // 1024}KB")


if __name__ == "__main__":
  main()
