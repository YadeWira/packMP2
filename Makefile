# unpackmp2 - lossless MPEG audio Layer II transform
# Makefile for Linux (native) and Windows (MinGW cross-compile)

CC      ?= gcc
CFLAGS  ?= -Wall -O2 -fomit-frame-pointer -s
LDFLAGS ?=

SRCDIR  = src
OBJDIR  = build

SOURCES = $(SRCDIR)/main.c $(SRCDIR)/globals.c $(SRCDIR)/bitio.c \
          $(SRCDIR)/frame.c $(SRCDIR)/pack.c $(SRCDIR)/unpack.c
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

TARGET  = unpackmp2

.PHONY: all clean dist mingw mingw64

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/unpackmp2.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

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
	rm -rf $(OBJDIR) $(TARGET) unpackmp2.exe

dist: clean
	tar czf unpackmp2-src.tar.gz --transform 's,^,unpackmp2/,' \
		src/ tools/ lpaq8_stdinout/ README.md gpl-3.0.txt Makefile
