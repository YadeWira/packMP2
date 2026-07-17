# packMP2

Lossless MPEG Audio Layer II (MP2) transformation + compression.
Layer II only — Layer I (MP1) files are rejected cleanly.

**packMP2** is a sub-project of **packMP3**. It provides the MP2 layer
codec: frame reordering (unpackmp2) + optimized compression (TCAM2).
Integrated as a submodule/library in packMP3.

Prebuilt binaries and static libs are in [GitHub Releases](https://github.com/YadeWira/packMP2/releases)
for Linux x64, Windows x64, and Windows x86.

**unpackmp2**: Reorders MP2 frames into the structured `um2` format —
more compressible with general-purpose compressors. Roundtrip is
byte-exact for the complete file (v1.2 preserves ID3 tags, padding, etc.).

**TCAM2** (Tovy Compresor de Audio MP2): Domain-optimized compressor for
um2 files. Two backends:
- **zstd + trained dict** (110 KB, default): ~90% ratio at ~0.01s — 100× faster than lpaq8
- **zpaq context-mixing**: custom-tuned ZPAQL methods, matches lpaq8 ratio at 17% faster

Copyright (C) 2009-2010 Michael Henke (unpackmp2) — GPLv3.
Copyright (C) 2026 Tovy (TCAM2, zpaq tuning) — GPLv3.

## How it works

```
mp2 ──[unpack]──> um2 ──[zpaq / zstd]──> compressed (TCAM2)
                    │
                    └──[pack]──> mp2  (byte-identical)
```

`unpackmp2` decomposes each MP2 frame into its semantic fields (bit allocations,
scalefactors, samples) and serializes them grouped by type across frames within
a block. This reordering exposes redundancy that generic compressors exploit,
yielding 5–15% better ratios vs compressing the raw MP2 directly.

Non-audio data (ID3 tags, RIFF headers) is preserved. Roundtrip is byte-exact.

## Build

```sh
make              # Full build (zero deps, vendored zstd+zpaq) → ./packmp2
make lib          # Static library (no CLI) → libpackmp2.a
make lite         # Unpack/pack only (just gcc+make, no compression)
make mingw        # Windows 32-bit cross-compile (full)
make mingw64      # Windows 64-bit cross-compile (full)
make mingw-lib    # Windows 32-bit static library
make mingw64-lib  # Windows 64-bit static library
make zpaq-fast    # Standalone zpaq bench tool (supports custom methods)
make clean
```

100% self-contained. `gcc` + `g++` + `make` is all you need.
zstd 1.5.7 vendored in `vendor/zstd/`, libzpaq 7.12 in `vendor/zpaq/`.

## Library API (v0.5+)

```c
#include "packmp2.h"

packmp2_opts opts = packmp2_opts_default();
opts.method = PACKMP2_METHOD_ZPAQ;
opts.level  = PACKMP2_LEVEL_BEST;  // 5 = matches lpaq8

unsigned char *out; size_t out_len; char msg[256];
packmp2_compress(mp2_data, mp2_len, &out, &out_len, &opts, msg);
// out is malloc'd — caller frees with free()
```

Link: `-L<packMP2_dir> -lpackmp2 -lstdc++ -lpthread`

**Thread-safe (v0.6+)**: fully reentrant, safe for concurrent calls. PackMP3
measured 3.4× speedup with `-th8` after removing the mutex.

Full header: `src/lib/packmp2.h`.

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
  -i, --input F     Read from file (default: stdin)
  -o, --output F    Write to file (default: stdout)
  -q, --quiet       Suppress progress messages
  -l, --level N     Compression level 1-9 (default: 1)
  -O, --optimized   SCFSI packing + scalefactor delta
  --zpaq N          Use zpaq context-mixing level 1-5 (best ratio)
  --zpaq-method S   Raw ZPAQL method string (advanced)
  -b, --benchmark   Report timing + ratio
  -s, --stats       Show detailed statistics
  --compare         Compress with/without dict, compare
  --no-dict         Compress without dictionary
  --dict FILE       Use external dictionary
  --list FILE       Show file metadata
  --test-all DIR    Batch test all .mp2 files in directory
  --csv             CSV output for scripting
  --raw             c/d passthrough (testing)
  --verify          Auto roundtrip verification (pipe mode)
  -V, --version     Print version
  -h, --help        This help
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
├── main.c           Unified CLI
└── lib/
    ├── packmp2.h     Public C API header
    ├── packmp2.c     Library implementation (mp2 ↔ compressed)
    ├── unpackmp2.h   Types, tables, declarations
    ├── globals.c     MPEG constants, allocation tables
    ├── bitio.c       Bit-level I/O (fbgetbits/fbputbits)
    ├── frame.c       Frame header parsing, CRC-16
    ├── pack.c        Read um2, repack to MP2 (+ packFrame)
    ├── unpack.c      Decompose MP2, write um2
    ├── tcam2.h       TCAM2 API
    ├── tcam2_enc.c   TCAM2 encoder (zstd / zpaq)
    ├── tcam2_dec.c   TCAM2 decoder
    ├── tcam2_dict.h  Trained zstd dictionary (110 KB)
    └── lite_stubs.c  Stubs for lite build (unpack/pack only)
vendor/
├── zstd/            Vendored zstd 1.5.7
└── zpaq/            libzpaq 7.12 + C wrapper (zpaq_c.h/cpp) + zpaq-fast
reference/           Legacy files (original sources, binaries, tools)
```

## Benchmarks (example.mp2, 691 KB, 160 kbps stereo)

### ZPAQ levels (v0.6 custom-tuned methods, compressing um2)

| Level | Method | Compressed | Ratio | Time |
|-------|--------|-----------|-------|------|
| 3 | BWT+1ISSE+mix | 574,046 | 83.1% | 323ms |
| 4 | BWT+c256+2ISSE+mix | 566,076 | 81.9% | 462ms |
| 5 | BWT+c256+2ISSE+MATCH+sparse+mm16+SSE | 561,834 | **81.3%** | 2365ms |
| lpaq8 -6 (ref) | — | ~561,500 | ~81.2% | — |

### Full pipeline (mp2 → TCAM2, 10-byte header included)

| Method | Compressed | Ratio | Time |
|--------|-----------|-------|------|
| TCAM2 zstd+dict | 623,056 | 90.1% | ~0.01s |
| zpaq level 3 | 574,046 | 83.1% | ~0.35s |
| zpaq level 4 | 566,076 | 81.9% | ~0.46s |
| zpaq level 5 | 561,834 | **81.3%** | ~2.37s |

Key takeaways:
- **zpaq m5** matches lpaq8 ratio at 17% faster (custom method, no built-in overhead)
- **zpaq m4** best speed/ratio sweet spot — only 27ms slower than m3 for +0.6% ratio
- **TCAM2 zstd+dict** 100× faster than lpaq8 for real-time use
- All levels pass byte-exact roundtrip on 19-file corpus

### External compressor comparison (125.8 MB test file, original unpackmp2 README)

| Pipeline | Options | Compressed size |
|----------|---------|----------------|
| unpackmp2 \| lpaq8 | 5 | 110.9 MB (84.0%) |
| unpackmp2 \| 7z LZMA | ultra | 117.9 MB (89.4%) |
| unpackmp2 \| 7z Bzip2 | ultra | 119.1 MB (90.3%) |
| unpackmp2 \| 7z PPMd | ultra | 120.6 MB (91.4%) |
| lpaq8 (no unpack) | 5 | 123.3 MB (93.5%) |

## Changelog

See [GitHub Releases](https://github.com/YadeWira/packMP2/releases).

- **v0.6** — Thread-safety: heap allocation, fully reentrant. ZPAQL retune (matches lpaq8).
- **v0.5** — Public C API, never-expand guard, zpaq backend, cross-platform libs.
- **v0.4** — Optimized mode (SCFSI packing + scalefactor delta).
- **v0.3** — TCAM2 zstd+dict compression.
- **v0.2** — CLI switches, --zpaq flag.
- **v0.1** — Initial: unpack/pack only.

## License

GNU GPL v3 — see `LICENSE`.

unpackmp2 uses code/ideas from: amp11 by Niklas Beisert, libmad by
Underbit Technologies, Inc., libtwolame by TwoLAME Authors.

lpaq8 compressor (C) 2007 Matt Mahoney, Alexander Ratushnyak.
