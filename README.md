# unpackmp2

Lossless transformation of MPEG Audio Layer II (MP2) data.

Unpacks MP2 frames into a structured intermediate format (`um2`) — more
compressible with general-purpose compressors — and repacks back to MP2,
bit-identical for the audio payload.

Copyright (C) 2009 Michael Henke — GPLv3.

## How it works

```
mp2 ──[unpack]──> um2 ──[lpaq8 / 7z / ...]──> compressed
                    │
                    └──[pack]──> mp2  (bit-identical audio)
```

`unpackmp2` decomposes each MP2 frame into its semantic fields (bit allocations,
scalefactors, samples) and serializes them grouped by type across frames within
a block. This reordering exposes redundancy that generic compressors exploit,
yielding 5–15% better ratios vs compressing the raw MP2 directly.

Non-audio data (ID3 tags, RIFF headers) is discarded during unpack — only raw
MP2 frames survive the roundtrip.

## Build

### Linux (native)

```sh
make
```

Requires `gcc` and `make`. Output: `./unpackmp2`.

### Windows (cross-compile)

```sh
make mingw    # 32-bit (i686-w64-mingw32-gcc)
make mingw64  # 64-bit (x86_64-w64-mingw32-gcc)
```

### Clean

```sh
make clean
```

## Usage

```
unpack mp2 to um2:  unpackmp2 u < input.mp2 > output.um2
pack um2 to mp2:    unpackmp2 p < input.um2 > output.mp2
```

Piping example with lpaq8 (compressor included in `lpaq8_stdinout/`):

```sh
# compress
unpackmp2 u < input.mp2 | lpaq8 5 - output.um2.lpaq8

# decompress
lpaq8 d input.um2.lpaq8 - | unpackmp2 p > output.mp2
```

Windows batch helpers: `tools/compressMP2LPAQ8.cmd`, `tools/decompressMP2LPAQ8.cmd`.

## Project structure

```
src/
├── unpackmp2.h   Common types, lookup tables, function declarations
├── globals.c      Global data tables (MPEG constants, bit allocation tables)
├── bitio.c        Low-level bit I/O (fbgetbits / fbputbits)
├── frame.c        Frame header parsing, CRC-16
├── pack.c         Read um2, repack to MP2 bitstream
├── unpack.c       Decompose MP2 frames, write um2
└── main.c         Entry point (argv[1] selects pack/unpack mode)
tools/             Windows .cmd helper scripts
lpaq8_stdinout/    Modified lpaq8 compressor (stdin/stdout variant)
```

## Test results (from original README)

`test1.mp2` (125.8 MB, 54m57s, 320 kbps, 48 kHz, stereo, DVB-S radio):

| Pipeline              | Options         | Compressed size |
|-----------------------|-----------------|-----------------|
| unpackmp2 \| lpaq8     | 5               | 110.9 MB (84.0%) |
| unpackmp2 \| 7z LZMA  | ultra           | 117.9 MB (89.4%) |
| unpackmp2 \| 7z Bzip2 | ultra           | 119.1 MB (90.3%) |
| unpackmp2 \| 7z PPMd  | ultra           | 120.6 MB (91.4%) |
| lpaq8 (no unpack)     | 5               | 123.3 MB (93.5%) |

## License

GNU GPL v3 — see `gpl-3.0.txt`.

The original `unpackmp2` uses code/ideas from:
- amp11 by Niklas Beisert
- libmad by Underbit Technologies, Inc.
- libtwolame by TwoLAME Authors

lpaq8 compressor (C) 2007 Matt Mahoney, Alexander Ratushnyak.
