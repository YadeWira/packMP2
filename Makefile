# packMP2 — MPEG Audio Layer II lossless transform + compression
# Makefile for Linux (native) and Windows (MinGW cross-compile)

CC      ?= gcc
CFLAGS  ?= -Wall -O2 -fomit-frame-pointer -s
LDFLAGS ?=

SRCDIR  = src
LIBDIR  = $(SRCDIR)/lib
OBJDIR  = build

# Library sources (linked by both unpackmp2 and TCAM2)
LIB_SRC  = $(LIBDIR)/globals.c $(LIBDIR)/bitio.c $(LIBDIR)/frame.c \
           $(LIBDIR)/pack.c $(LIBDIR)/unpack.c
LIB_OBJ  = $(patsubst $(LIBDIR)/%.c,$(OBJDIR)/%.o,$(LIB_SRC))

# TCAM2 sources (uses library objects)
TCAM2_SRC = $(LIBDIR)/tcam2_enc.c $(LIBDIR)/tcam2_dec.c
TCAM2_OBJ = $(patsubst $(LIBDIR)/%.c,$(OBJDIR)/%.o,$(TCAM2_SRC))

# Main entry point
MAIN_OBJ  = $(OBJDIR)/main.o

TARGET    = packmp2
ALL_OBJ   = $(MAIN_OBJ) $(LIB_OBJ) $(TCAM2_OBJ)

.PHONY: all clean dist mingw mingw64

all: $(TARGET)

$(TARGET): $(ALL_OBJ)
	$(CC) $(ALL_OBJ) $(LDFLAGS) -lzstd -o $@

$(OBJDIR)/%.o: $(LIBDIR)/%.c $(LIBDIR)/unpackmp2.h | $(OBJDIR)
	$(CC) $(CFLAGS) -I$(LIBDIR) -c $< -o $@

$(OBJDIR)/main.o: $(SRCDIR)/main.c $(LIBDIR)/unpackmp2.h $(LIBDIR)/tcam2.h | $(OBJDIR)
	$(CC) $(CFLAGS) -I$(LIBDIR) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

# MinGW 32-bit cross-compile
mingw: CC = i686-w64-mingw32-gcc
mingw: CFLAGS = -Wall -O2 -fomit-frame-pointer -static-libgcc -s
mingw: TARGET = packmp2.exe
mingw: all

# MinGW 64-bit cross-compile
mingw64: CC = x86_64-w64-mingw32-gcc
mingw64: CFLAGS = -Wall -O2 -fomit-frame-pointer -static-libgcc -s
mingw64: TARGET = packmp2.exe
mingw64: all

clean:
	rm -rf $(OBJDIR) packmp2 packmp2.exe

dist: clean
	tar czf packmp2-src.tar.gz --transform 's,^,packmp2/,' \
		src/ reference/ LICENSE Makefile README.md .gitignore
