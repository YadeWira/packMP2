# unpackmp2 + TCAM2

Lossless MPEG Audio Layer II (MP2) transformation + compression.

**packMP2** is a sub-project of **packMP3**. It provides the MP2 layer
codec: frame reordering (unpackmp2) + optimized compression (TCAM2).
It will be integrated as a submodule/library in packMP3.

Binaries included in the repo (`unpackmp2.exe`, `lpaq8.exe`, etc.) are
for development/testing only. In production, packMP3 will link against
the libraries directly.

**unpackmp2**: Reorders MP2 frames into the structured `um2` format —
more compressible with general-purpose compressors. Roundtrip is
byte-exact for the audio payload (v1.2 preserves ID3 tags, padding, etc.).

**TCAM2** (Tovy Compresor de Audio MP2): Domain-optimized compressor for
um2 files. Uses zstd level 1 with a 110 KB dictionary trained across
multiple MP2 samples. 131x faster than lpaq8 with only a 3.7 point
ratio gap.

Copyright (C) 2009-2010 Michael Henke (unpackmp2) — GPLv3.
Copyright (C) 2026 Tovy (TCAM2) — GPLv3.

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

Non-audio data (ID3 tags, RIFF headers) is preserved in v1.2 format.
Roundtrip is byte-exact for the complete file.

## Build

```sh
make          # Linux native → ./packmp2
make mingw    # Windows 32-bit cross-compile
make mingw64  # Windows 64-bit cross-compile
make clean    # Remove build artifacts
```

Requires `gcc`, `make`, `libzstd-dev`.

## Usage

```
packmp2 u < in.mp2 > out.um2       unpack:   mp2 -> um2
packmp2 p < in.um2 > out.mp2       pack:     um2 -> mp2
packmp2 c < in.um2 > out.tcam2     compress: um2 -> tcam2
packmp2 d < in.tcam2 > out.um2     decompress: tcam2 -> um2
```

Full pipeline:
```sh
packmp2 u < input.mp2 | packmp2 c | packmp2 d | packmp2 p > output.mp2
```

## Project structure

```
src/
├── main.c         Unified CLI (unpack/pack/compress/decompress)
└── lib/
    ├── unpackmp2.h   Types, tables, declarations
    ├── globals.c      MPEG constants, allocation tables
    ├── bitio.c        Bit-level I/O (fbgetbits/fbputbits)
    ├── frame.c        Frame header parsing, CRC-16
    ├── pack.c         Read um2, repack to MP2 (+ packFrame)
    ├── unpack.c       Decompose MP2, write um2
    ├── tcam2.h        TCAM2 API
    ├── tcam2_enc.c    TCAM2 encoder (zstd + dictionary)
    ├── tcam2_dec.c    TCAM2 decoder (zstd)
    └── tcam2_dict.h   Trained zstd dictionary (110 KB, 5 samples)
reference/         Legacy files (original sources, binaries, tools)
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

## TCAM2 — Tovy Compresor de Audio MP2

Domain-optimized compressor for um2 files. Uses zstd level 1 with a
pre-trained 110KB dictionary for fast, high-ratio compression.

### Usage

```
# Compress (pipe-friendly)
packmp2 u < input.mp2 | packmp2 c > output.tcam2

# Decompress
packmp2 d < input.tcam2 | packmp2 p > output.mp2
```

### Benchmarks (example.mp2, 691 KB, 160kbps stereo)

| Method | Compressed | Ratio | Encode | Decode |
|--------|-----------|-------|--------|--------|
| lpaq8 5 via um2 | 561,496 | 81.2% | 1.36s | 1.84s |
| **TCAM2 (dict+zstd)** | **586,846** | **84.9%** | **0.010s** | **0.007s** |
| zstd -1 via um2 | 641,658 | 92.8% | 0.014s | 0.009s |
| gzip -6 via um2 | 655,177 | 94.8% | 0.29s | 0.009s |

TCAM2 is **131x faster** than lpaq8 at only 3.7 points worse ratio.
All 13 test samples pass byte-exact roundtrip.

### Project structure

```
src/
├── unpackmp2.h   Types, constants, declarations
├── globals.c      MPEG tables, global frame buffer
├── bitio.c        Bit-level I/O (fbgetbits/fbputbits)
├── frame.c        Frame header parsing, CRC-16
├── pack.c         Read um2, repack to MP2 (+ packFrame)
├── unpack.c       Decompose MP2 frames, write um2
├── main.c         unpackmp2 entry point
├── tcam2.h        TCAM2 API
├── tcam2.c        TCAM2 CLI entry point
├── tcam2_enc.c    TCAM2 encoder (zstd + dictionary)
├── tcam2_dec.c    TCAM2 decoder (zstd + dictionary)
└── tcam2_dict.h   Trained zstd dictionary (110 KB, 5 samples)
tools/             Windows .cmd helper scripts (testing)
lpaq8_stdinout/    Modified lpaq8 (stdin/stdout, reference/testing)
build/             Build artifacts (not tracked)
```

**Note on binaries:** The executables in this repo (`unpackmp2.exe`,
`lpaq8.exe`, `lpaq8_stdinout/`) are the original Windows builds (2009-2010),
included for reference and testing. They are not needed on Linux — `make`
produces native binaries. For packMP3, only the `.o` files from `pack.c`,
`unpack.c`, `frame.c`, `bitio.c`, and `globals.c` will be linked.
