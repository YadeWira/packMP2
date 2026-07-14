# unpackmp2 - lossless MPEG audio Layer II transform
# Makefile for Linux (native) and Windows (MinGW cross-compile)

CC      ?= gcc
CXX     ?= g++
CFLAGS  ?= -Wall -O2 -fomit-frame-pointer -s
CXXFLAGS ?= -O2 -fomit-frame-pointer -s
LDFLAGS ?=

SRCDIR  = src
OBJDIR  = build

# unpackmp2 sources
SOURCES = $(SRCDIR)/main.c $(SRCDIR)/globals.c $(SRCDIR)/bitio.c \
          $(SRCDIR)/frame.c $(SRCDIR)/pack.c $(SRCDIR)/unpack.c
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

TARGET  = unpackmp2

# TCAM2 sources (reuses globals.o + frame.o from unpackmp2)
TCAM2_SRC = $(SRCDIR)/tcam2.c $(SRCDIR)/tcam2_enc.c $(SRCDIR)/tcam2_dec.c
TCAM2_OBJ = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(TCAM2_SRC)) \
            $(OBJDIR)/globals.o $(OBJDIR)/frame.o
TCAM2     = tcam2

# lpaq compressors
LPQ8      = lpaq8
LPQ9      = lpaq9m

.PHONY: all clean dist mingw mingw64 tcam2 lpaq8 lpaq9m

all: $(TARGET) $(TCAM2)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(TCAM2): $(TCAM2_OBJ)
	$(CC) $(TCAM2_OBJ) $(LDFLAGS) -lz -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/unpackmp2.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/tcam2_enc.o $(OBJDIR)/tcam2_dec.o: $(SRCDIR)/tcam2.h

$(OBJDIR):
	mkdir -p $(OBJDIR)

# lpaq8 (32-bit — original code assumes 32-bit integers)
lpaq8: $(LPQ8)
$(LPQ8):
	g++ -m32 $(CXXFLAGS) lpaq8_stdinout/lpaq8_stdinout.cpp -o $@ 2>/dev/null || \
	g++ $(CXXFLAGS) lpaq8_stdinout/lpaq8_stdinout.cpp -o $@

# lpaq9m (64-bit safe)
lpaq9m: $(LPQ9)
$(LPQ9):
	$(CXX) $(CXXFLAGS) lpaq9m/lpaq9m_stdinout.cpp -o $@

# MinGW 32-bit cross-compile
mingw: CC = i686-w64-mingw32-gcc
mingw: CFLAGS = -Wall -O2 -fomit-frame-pointer -static-libgcc -s
mingw: TARGET = unpackmp2.exe
mingw: all

# MinGW 64-bit cross-compile
mingw64: CC = x86_64-w64-mingw32-gcc
mingw64: CFLAGS = -Wall -O2 -fomit-frame-pointer -static-libgcc -s
mingw64: TARGET = unpackmp2.exe
mingw64: all

clean:
	rm -rf $(OBJDIR) $(TARGET) unpackmp2.exe $(TCAM2) $(LPQ8) $(LPQ9)

dist: clean
	tar czf unpackmp2-src.tar.gz --transform 's,^,unpackmp2/,' \
		src/ tools/ lpaq8_stdinout/ README.md gpl-3.0.txt Makefile
