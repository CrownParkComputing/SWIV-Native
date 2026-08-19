CC ?= cc
CFLAGS = -O2 -std=c11 -Wall -Wextra -D_DEFAULT_SOURCE -D_GNU_SOURCE
RAYLIB = -I$(HOME)/.local/include $(HOME)/.local/lib/libraylib.a -lm -lpthread -ldl -lGL -lX11

all: build/dumpmap build/swivview build/simrun

build/dumpmap: tools/dumpmap.c src/swivdata.c src/swivdata.h
	mkdir -p build && $(CC) $(CFLAGS) -o $@ tools/dumpmap.c src/swivdata.c

ENGINE = src/engine/engine.c src/engine/effects.c src/engine/coro.c src/engine/tables.c src/engine/player_stub.c src/behaviours/table.c $(wildcard src/behaviours/bh_*.c)

build/swivview: src/viewer.c src/swivdata.c src/swivdata.h $(ENGINE) src/engine/engine.h
	mkdir -p build && $(CC) $(CFLAGS) -o $@ src/viewer.c src/swivdata.c $(ENGINE) $(RAYLIB)

build/simrun: tools/simrun.c src/swivdata.c $(ENGINE) src/engine/engine.h
	mkdir -p build && $(CC) $(CFLAGS) -o $@ tools/simrun.c src/swivdata.c $(ENGINE) -lm

test: build/dumpmap
	./tools/test_native_vs_python.sh

clean:
	rm -rf build
