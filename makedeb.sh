#!/bin/bash
# Build .deb package for packMP2
# Usage: ./makedeb.sh <version> [arch]
#   version: e.g. 0.7.0
#   arch: amd64 (default) or i386

set -e

VERSION="${1:-0.7.0}"
ARCH="${2:-amd64}"
PKG="packmp2_${VERSION}_${ARCH}"
DEB="${PKG}.deb"

echo "==> Building packMP2 v${VERSION} for ${ARCH}"

# Build binary
make clean >/dev/null 2>&1
make -j$(nproc) 2>&1 | tail -1
make lib 2>&1 | tail -1

# Create .deb structure
rm -rf "_deb"
mkdir -p "_deb/DEBIAN"
mkdir -p "_deb/usr/bin"
mkdir -p "_deb/usr/lib"
mkdir -p "_deb/usr/include"
mkdir -p "_deb/usr/share/doc/packmp2"
mkdir -p "_deb/usr/share/man/man1"

# DEBIAN/control
cat > "_deb/DEBIAN/control" << EOF
Package: packmp2
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Depends: libc6 (>= 2.34), libstdc++6 (>= 5)
Maintainer: Tovy <packmp2@yadewira.github.io>
Homepage: https://github.com/YadeWira/packMP2
Description: MPEG Audio Layer I/II lossless transform + compression
 packMP2 reorders MPEG Layer I/II audio frames into the structured um2
 format and compresses them with zstd (fast, ~90%) or zpaq context-mixing
 (best, ~81%, matches lpaq8). Fully lossless — roundtrip is byte-exact.
 .
 Includes CLI tool, static library (libpackmp2.a), and C header.
EOF

# Install files
cp packmp2 "_deb/usr/bin/"
cp libpackmp2.a "_deb/usr/lib/"
cp src/lib/packmp2.h "_deb/usr/include/"
cp LICENSE "_deb/usr/share/doc/packmp2/copyright"

# Changelog
cat > "_deb/usr/share/doc/packmp2/changelog.Debian" << EOF
packmp2 (${VERSION}) stable; urgency=medium

  * See https://github.com/YadeWira/packMP2/releases/tag/v${VERSION}

 -- Tovy <packmp2@yadewira.github.io>  $(date -R)
EOF
gzip -9nf "_deb/usr/share/doc/packmp2/changelog.Debian"

# Man page
cat > "_deb/usr/share/man/man1/packmp2.1" << 'EOFM'
.TH PACKMP2 1
.SH NAME
packmp2 \- MPEG Audio Layer I/II lossless transform + compression
.SH SYNOPSIS
.B packmp2
<command> [options]
.SH COMMANDS
u, unpack      mp2/mp1 -> um2
p, pack        um2 -> mp2/mp1
c, compress    um2 -> tcam2
d, decompress  tcam2 -> um2
x, pipe        mp2/mp1 -> um2 -> tcam2 -> um2 -> mp2/mp1
.SH SEE ALSO
https://github.com/YadeWira/packMP2
EOFM
gzip -9f "_deb/usr/share/man/man1/packmp2.1"

# Strip binary
strip "_deb/usr/bin/packmp2" 2>/dev/null || true

# Build .deb
dpkg-deb --root-owner-group --build "_deb" "${DEB}"
echo "==> ${DEB} built ($(du -h ${DEB} | cut -f1))"

# Cleanup
rm -rf "_deb"
