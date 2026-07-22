# packMP2 — MPEG Audio Layer II lossless transform + compression
# Makefile for Linux (native) and Windows (MinGW cross-compile)
# Vendored zstd + zpaq — zero external dependencies.

CC      ?= gcc
CXX     ?= g++
AR      ?= ar
CFLAGS  ?= -Wall -O3 -march=native -fomit-frame-pointer -s -I$(LIBDIR) -Ivendor/zstd -Ivendor/zstd/common -Ivendor/zstd/compress -Ivendor/zstd/decompress -Ivendor/zpaq -DXXH_NAMESPACE=ZSTD_ -DZSTD_LEGACY_SUPPORT=0 -DDYNAMIC_BMI2=0 -DZSTD_DISABLE_ASM
CXXFLAGS ?= -Wall -O3 -march=native -fomit-frame-pointer -Ivendor/zpaq
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
           $(LIBDIR)/pack.c $(LIBDIR)/unpack.c $(LIBDIR)/packmp2.c \
           $(LIBDIR)/um2_delta_enc.c $(LIBDIR)/um2_delta_dec.c
LIB_OBJ  = $(patsubst $(LIBDIR)/%.c,$(OBJDIR)/%.o,$(LIB_SRC))

# Standalone um2_delta CLI tool (optional)
UM2_DELTA_SRC = $(LIBDIR)/um2_delta.c
UM2_DELTA_OBJ = $(patsubst $(LIBDIR)/%.c,$(OBJDIR)/%.o,$(UM2_DELTA_SRC))
UM2_DELTA_BIN = um2_delta_tool

# um2_delta CLI links against libpackmp2 (needs extractFrameHeaderInfo, ALLOC, etc.)
# Note: renamed to um2_delta_tool to avoid conflict with src/lib/um2_delta.c → build/um2_delta.o
um2_delta_tool: $(UM2_DELTA_OBJ) lib
	$(CC) $(CFLAGS) $(UM2_DELTA_OBJ) -L. -lpackmp2 -lm -lpthread -o $@
	@echo "  um2_delta_tool CLI ready (use: um2_delta_tool e/d [keyframe_interval])"

# TCAM2 sources
TCAM2_SRC = $(LIBDIR)/tcam2_enc.c $(LIBDIR)/tcam2_dec.c
TCAM2_OBJ = $(patsubst $(LIBDIR)/%.c,$(OBJDIR)/%.o,$(TCAM2_SRC))

# zpaq backend (C++ → linked with g++)
ZPAQ_DIR  = vendor/zpaq
ZPAQ_SRC  = $(ZPAQ_DIR)/zpaq_c.cpp $(ZPAQ_DIR)/libzpaq.cpp
ZPAQ_OBJ  = $(patsubst $(ZPAQ_DIR)/%.cpp,$(OBJDIR)/zpaq_%.o,$(ZPAQ_SRC))

MAIN_OBJ  = $(OBJDIR)/main.o
TARGET    = packmp2
ALL_OBJ   = $(MAIN_OBJ) $(LIB_OBJ) $(TCAM2_OBJ) $(ZSTD_OBJ) $(ZPAQ_OBJ)

.PHONY: all lib clean dist mingw mingw64 mingw-lib mingw64-lib lite zpaq-fast um2_delta_tool

all: $(TARGET)

$(TARGET): $(ALL_OBJ)
	$(CXX) $(ALL_OBJ) $(LDFLAGS) -lm -lpthread -o $@

# Static library for external consumers (e.g. packMP3).
# Produces libpackmp2.a — link with: -L<dir> -lpackmp2 -lstdc++ -lpthread
# Public header: src/lib/packmp2.h
LIBOBJ = $(LIB_OBJ) $(TCAM2_OBJ) $(ZSTD_OBJ) $(ZPAQ_OBJ)
lib: $(LIBOBJ)
	$(AR) rcs libpackmp2.a $(LIBOBJ)
	@echo "  libpackmp2.a ready ($(words $(LIBOBJ)) objects)"

# Standalone um2_delta CLI tool (stdin/stdout delta encoding for um2 v1.3)
um2_delta: $(UM2_DELTA_OBJ) lib
	$(CC) $(CFLAGS) $(UM2_DELTA_OBJ) -L. -lpackmp2 -lm -lpthread -o $@
	@echo "  um2_delta CLI ready (use: um2_delta e/d [keyframe_interval])"

# Lite build objects (defined globally so prerequisites can see them)
LITE_OBJ = $(MAIN_OBJ) $(LIB_OBJ) $(OBJDIR)/lite_stubs.o

# Lite build: unpack/pack only, no zstd/zpaq needed (pure C)
lite: CFLAGS += -DNO_ZPAQ -DNO_ZSTD
lite: $(LITE_OBJ)
	$(CC) $(LITE_OBJ) $(LDFLAGS) -o packmp2
	@echo "  Built packmp2 (lite: unpack/pack only, no TCAM2)"

$(OBJDIR)/%.o: $(LIBDIR)/%.c $(LIBDIR)/unpackmp2.h $(LIBDIR)/packmp2.h $(LIBDIR)/tcam2.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/main.o: $(SRCDIR)/main.c $(LIBDIR)/unpackmp2.h $(LIBDIR)/tcam2.h vendor/zpaq/zpaq_c.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $(SRCDIR)/main.c -o $@

$(OBJDIR)/lite_stubs.o: $(LIBDIR)/lite_stubs.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Vendored zstd build (C)
$(OBJDIR)/zstd_%.o: $(ZSTD_DIR)/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Vendored zpaq build (C++)
$(OBJDIR)/zpaq_%.o: $(ZPAQ_DIR)/%.cpp $(ZPAQ_DIR)/zpaq_c.h | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Windows cross-compile (full TCAM2 via vendored zstd + zpaq, static link)
# NOTE: -lpthread removed — zpaq single-stream doesn't need threads;
# winpthreads wrapper overhead was contributing to ~2x slowdown vs Linux.
mingw:
	$(MAKE) clean >/dev/null && $(MAKE) CC=i686-w64-mingw32-gcc CXX=i686-w64-mingw32-g++ TARGET=packmp2.exe LDFLAGS="-static" all

mingw64:
	$(MAKE) clean >/dev/null && $(MAKE) CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ TARGET=packmp2.exe LDFLAGS="-static" all

# Cross-compiled static library for Windows consumers (e.g. packMP3)
mingw-lib:
	$(MAKE) clean >/dev/null && $(MAKE) CC=i686-w64-mingw32-gcc CXX=i686-w64-mingw32-g++ AR=i686-w64-mingw32-ar lib
	@echo "  -> libpackmp2.a (win32)"

mingw64-lib:
	$(MAKE) clean >/dev/null && $(MAKE) CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ AR=x86_64-w64-mingw32-ar lib
	@echo "  -> libpackmp2.a (win64)"

clean:
	rm -rf $(OBJDIR) packmp2 packmp2.exe zpaq-fast um2_delta_tool

# zpaq-fast: standalone CLI (single-stream, no journaling)
zpaq-fast:
	$(CXX) $(CXXFLAGS) vendor/zpaq/zpaq_fast.cpp vendor/zpaq/libzpaq.cpp -lpthread -o $@

dist: clean
	tar czf packmp2-src.tar.gz --transform 's,^,packmp2/,' \
		src/ reference/ LICENSE Makefile README.md .gitignore vendor/
