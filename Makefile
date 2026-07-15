# packMP2 — MPEG Audio Layer II lossless transform + compression
# Makefile for Linux (native) and Windows (MinGW cross-compile)
# Vendored zstd — zero external dependencies.

CC      ?= gcc
CFLAGS  ?= -Wall -O2 -fomit-frame-pointer -s -I$(LIBDIR) -Ivendor/zstd -Ivendor/zstd/common -Ivendor/zstd/compress -Ivendor/zstd/decompress -DXXH_NAMESPACE=ZSTD_ -DZSTD_LEGACY_SUPPORT=0 -DDYNAMIC_BMI2=0 -DZSTD_ENABLE_ASM_X86_64_BMI2=0
LDFLAGS ?=

SRCDIR  = src
LIBDIR  = $(SRCDIR)/lib
OBJDIR  = build

# Vendored zstd (self-contained, no system deps)
ZSTD_DIR = vendor/zstd
ZSTD_SRC = $(wildcard $(ZSTD_DIR)/common/*.c) \
           $(wildcard $(ZSTD_DIR)/compress/*.c) \
           $(wildcard $(ZSTD_DIR)/decompress/*.c)
ZSTD_OBJ = $(patsubst $(ZSTD_DIR)/%.c,$(OBJDIR)/zstd_%.o,$(ZSTD_SRC))

# Library sources
LIB_SRC  = $(LIBDIR)/globals.c $(LIBDIR)/bitio.c $(LIBDIR)/frame.c \
           $(LIBDIR)/pack.c $(LIBDIR)/unpack.c
LIB_OBJ  = $(patsubst $(LIBDIR)/%.c,$(OBJDIR)/%.o,$(LIB_SRC))

# TCAM2 sources
TCAM2_SRC = $(LIBDIR)/tcam2_enc.c $(LIBDIR)/tcam2_dec.c
TCAM2_OBJ = $(patsubst $(LIBDIR)/%.c,$(OBJDIR)/%.o,$(TCAM2_SRC))

MAIN_OBJ  = $(OBJDIR)/main.o
TARGET    = packmp2
ALL_OBJ   = $(MAIN_OBJ) $(LIB_OBJ) $(TCAM2_OBJ) $(ZSTD_OBJ)

.PHONY: all clean dist mingw mingw64 lite

all: $(TARGET)

$(TARGET): $(ALL_OBJ)
	$(CC) $(ALL_OBJ) $(LDFLAGS) -lm -o $@

# Lite build: unpack/pack only, no zstd needed
lite: LITE_OBJ = $(MAIN_OBJ) $(LIB_OBJ)
lite: $(LITE_OBJ)
	$(CC) $(LITE_OBJ) $(LDFLAGS) -o packmp2
	@echo "  Built packmp2 (lite: unpack/pack only, no TCAM2)"

$(OBJDIR)/%.o: $(LIBDIR)/%.c $(LIBDIR)/unpackmp2.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/main.o: $(SRCDIR)/main.c $(LIBDIR)/unpackmp2.h $(LIBDIR)/tcam2.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $(SRCDIR)/main.c -o $@

# Vendored zstd build
$(OBJDIR)/zstd_%.o: $(ZSTD_DIR)/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Windows cross-compile (full TCAM2 via vendored zstd)
mingw:
	$(MAKE) clean >/dev/null && $(MAKE) CC=i686-w64-mingw32-gcc TARGET=packmp2.exe all

mingw64:
	$(MAKE) clean >/dev/null && $(MAKE) CC=x86_64-w64-mingw32-gcc TARGET=packmp2.exe all

clean:
	rm -rf $(OBJDIR) packmp2 packmp2.exe

dist: clean
	tar czf packmp2-src.tar.gz --transform 's,^,packmp2/,' \
		src/ reference/ LICENSE Makefile README.md .gitignore
