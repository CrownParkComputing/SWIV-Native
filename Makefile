CC ?= cc
CFLAGS = -O2 -std=c11 -Wall -Wextra -D_DEFAULT_SOURCE -D_GNU_SOURCE
RAYLIB = -I$(HOME)/.local/include $(HOME)/.local/lib/libraylib.a -lm -lpthread -ldl -lGL -lX11

all: build/dumpmap build/swivview

build/dumpmap: tools/dumpmap.c src/swivdata.c src/swivdata.h
	mkdir -p build && $(CC) $(CFLAGS) -o $@ tools/dumpmap.c src/swivdata.c

build/swivview: src/viewer.c src/swivdata.c src/swivdata.h
	mkdir -p build && $(CC) $(CFLAGS) -o $@ src/viewer.c src/swivdata.c $(RAYLIB)

test: build/dumpmap
	./tools/test_native_vs_python.sh

clean:
	rm -rf build
