#!/usr/bin/env python3
"""Embed the short local MP3 decoder-test sample as a flash-resident array."""
import pathlib
import sys
src,dst=map(pathlib.Path,sys.argv[1:3])
data=src.read_bytes()
if not data or len(data)>65536:
    raise SystemExit('invalid MP3 test sample')
lines=['#pragma once','#include <stdint.h>','static const uint8_t kCodecSampleMp3[] PROGMEM = {']
for pos in range(0,len(data),20):
    lines.append('  '+','.join(f'0x{x:02x}' for x in data[pos:pos+20])+',')
lines += ['};',f'static constexpr uint32_t kCodecSampleBytes = {len(data)};','']
content='\n'.join(lines)
if not dst.exists() or dst.read_text()!=content: dst.write_text(content)
