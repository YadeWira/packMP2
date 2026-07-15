# packMP2 Lab

Lossless MPEG Audio Layer II (MP2) transformation + compression.

**packMP2** is a sub-project of **packMP3**. It provides the MP2 layer
codec: frame reordering (unpackmp2) + optimized compression (TCAM2).
It will be integrated as a submodule/library in packMP3.

Prebuilt binaries are in [GitHub Releases](https://github.com/YadeWira/packMP2/releases)
for Linux x64, Windows x64, and Windows x86. All include full TCAM2 support.
Binaries in `reference/` are the original Windows builds (2009-2010) kept
for historical reference.

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
make          # Full build (zero deps, vendored zstd) → ./packmp2
make lite     # Unpack/pack only (just gcc+make)
make mingw    # Windows 32-bit cross-compile (full TCAM2)
make mingw64  # Windows 64-bit cross-compile (full TCAM2)
make clean    # Remove build artifacts
```

100% self-contained. `gcc` + `make` is all you need. zstd 1.5.7 is vendored
in `vendor/zstd/`.

## Usage

```
packmp2 <command> [options]

Commands:
  u, unpack      mp2 -> um2
  p, pack        um2 -> mp2
  c, compress    um2 -> tcam2
  d, decompress  tcam2 -> um2
  x, pipe        mp2 -> um2 -> tcam2 -> um2 -> mp2

Options:
  -i, --input F   Read from file (default: stdin)
  -o, --output F  Write to file (default: stdout)
  -q, --quiet     Suppress progress messages
  -l, --level N   zstd level 1-9 (default: 1)
  -b, --benchmark Report timing + ratio
  -s, --stats     Show detailed statistics
  --compare       Compress with/without dict, compare
  --no-dict       Compress without dictionary
  --dict FILE     Use external dictionary
  --list FILE     Show file metadata (no processing)
  --test-all DIR  Batch test all .mp2 files in directory
  --csv           CSV output for scripting
  --raw           c/d passthrough (testing)
  --verify        Auto roundtrip verification (pipe mode)
  -V, --version   Print version
  -h, --help      This help
```

Full pipeline:
```sh
packmp2 u < input.mp2 | packmp2 c | packmp2 d | packmp2 p > output.mp2
# Or with switches:
packmp2 x -i input.mp2 -o output.mp2 --verify -b
```

## Project structure

```
src/
├── main.c         Unified CLI
└── lib/
    ├── unpackmp2.h   Types, tables, declarations
    ├── globals.c      MPEG constants, allocation tables
    ├── bitio.c        Bit-level I/O (fbgetbits/fbputbits)
    ├── frame.c        Frame header parsing, CRC-16
    ├── pack.c         Read um2, repack to MP2 (+ packFrame)
    ├── unpack.c       Decompose MP2, write um2
    ├── tcam2.h        TCAM2 API
    ├── tcam2_enc.c    TCAM2 encoder
    ├── tcam2_dec.c    TCAM2 decoder
    └── tcam2_dict.h   Trained zstd dictionary (110 KB, 5 samples)
vendor/zstd/       Vendored zstd 1.5.7 (zero external deps)
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

## TCAM2 Benchmarks (example.mp2, 691 KB, 160kbps stereo)

| Method | Compressed | Ratio | Encode | Decode |
|--------|-----------|-------|--------|--------|
| lpaq8 5 via um2 | 561,496 | 81.2% | 1.36s | 1.84s |
| **packmp2 c (dict+zstd)** | **586,846** | **84.9%** | **0.010s** | **0.007s** |
| zstd -1 via um2 | 641,658 | 92.8% | 0.014s | 0.009s |
| gzip -6 via um2 | 655,177 | 94.8% | 0.29s | 0.009s |

TCAM2 is **131x faster** than lpaq8 with only 3.7 points ratio gap.
All 13 test samples pass byte-exact roundtrip.

For **maximum compression** (near lpaq8), pipe through zpaq:
```sh
packmp2 u < input.mp2 | zpaq a output.zpaq - -m5 -f
zpaq x output.zpaq -to - | packmp2 p > output.mp2
```
zpaq -m5 on um2 achieves **81.5%** (vs lpaq8 81.2%) at ~1.7s.

## License

GNU GPL v3 — see `LICENSE`.

unpackmp2 uses code/ideas from: amp11 by Niklas Beisert, libmad by
Underbit Technologies, Inc., libtwolame by TwoLAME Authors.

lpaq8 compressor (C) 2007 Matt Mahoney, Alexander Ratushnyak.
