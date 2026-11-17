#!/usr/bin/env python3
"""Embed the user-supplied 16 kHz mono signed-16 PCM radar cue in flash."""
import pathlib
import sys

src, dst = map(pathlib.Path, sys.argv[1:3])
data = src.read_bytes()
if not data or len(data) % 2:
    raise SystemExit("invalid PCM sample")
lines = ['#pragma once', '#include <stdint.h>',
         'static const uint8_t kRadarCuePcm[] PROGMEM = {']
for offset in range(0, len(data), 20):
    lines.append('  ' + ','.join(f'0x{x:02x}' for x in data[offset:offset+20]) + ',')
lines += ['};', f'static constexpr uint32_t kRadarCueBytes = {len(data)};', '']
contents = '\n'.join(lines)
if not dst.exists() or dst.read_text() != contents:
    dst.write_text(contents)
