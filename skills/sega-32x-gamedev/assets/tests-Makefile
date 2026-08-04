# Build the headless libretro test harness.
#   make            -> ./harness
# Then place the PicoDrive core at emu/picodrive_libretro.so (see
# references/testing.md) and run: python3 run_tests.py
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra
harness: harness.c
	$(CC) $(CFLAGS) -o $@ harness.c -ldl
clean:
	rm -f harness
.PHONY: clean
