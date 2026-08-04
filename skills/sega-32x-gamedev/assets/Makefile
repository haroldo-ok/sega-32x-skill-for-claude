# ======================================================================
# Sega 32X ROM build template  (Chilly Willy 32XDK 20220418, GCC 12.1)
#
#   make            build rom/$(TARGET).32x
#   make check      build + static verify + PicoDrive point-to-point tests
#   make clean
#
# Adjust TARGET, the object lists, and the marker strings for verify_rom.
# See references/toolchain-and-build.md for the full explanation.
# ======================================================================

GENDEV ?= /opt/toolchains/sega
TARGET  ?= game
TITLE   ?= MY 32X GAME
BUILD   := build
ROM     := rom/$(TARGET).32x

# ---- toolchains ------------------------------------------------------
SHPREFIX := $(GENDEV)/sh-elf/bin/sh-elf-
MDPREFIX := $(GENDEV)/m68k-elf/bin/m68k-elf-
CC   := $(SHPREFIX)gcc
AS   := $(SHPREFIX)as
OBJC := $(SHPREFIX)objcopy
SIZE := $(SHPREFIX)size
MDCC := $(MDPREFIX)gcc
MDOBJC := $(MDPREFIX)objcopy
HOSTCC ?= cc

GCCVER := 12.1.0

# ---- SH-2 flags ------------------------------------------------------
INCPATH := -Isrc/core -Isrc/platform/32x \
           -I$(GENDEV)/sh-elf/include -I$(GENDEV)/sh-elf/sh-elf/include
SHFLAGS := -c -std=c11 -m2 -mb -mtas -Os -fomit-frame-pointer \
           -ffunction-sections -fdata-sections -fno-common -flto \
           -Wall -Wextra -Wno-unused-parameter -D__32X__ -DMARS $(INCPATH)
# Timing-critical objects (PWM mixer, etc.) should override to: -O2 -fno-lto
SHAUDIOFLAGS := $(filter-out -Os -flto,$(SHFLAGS)) -O2 -fno-lto

LDFLAGS := -T src/platform/32x/mars.ld -nostdlib --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map -Os -flto
LIBS    := -L$(GENDEV)/sh-elf/sh-elf/lib \
           -L$(GENDEV)/sh-elf/lib/gcc/sh-elf/$(GCCVER) -lc -lgcc -lnosys

# ---- 68000 flags -----------------------------------------------------
MDFLAGS := -c -m68000 -std=c11 -Os -fomit-frame-pointer -ffunction-sections \
           -fdata-sections -Wa,--register-prefix-optional -Isrc/platform/32x/md_src
MDLDFLAGS := -T $(GENDEV)/ldscripts/mars-md.ld -nostdlib -Os
MDLIBS  := -L$(GENDEV)/m68k-elf/lib/gcc/m68k-elf/$(GCCVER) -lgcc

# ---- objects (EDIT) --------------------------------------------------
CORE_OBJS := $(BUILD)/game.o $(BUILD)/render.o        # src/core/*.c
PLAT_OBJS := $(BUILD)/main_32x.o $(BUILD)/hw_32x.o     # src/platform/32x/*.c
SH_OBJS   := $(BUILD)/mars_start.o $(CORE_OBJS) $(PLAT_OBJS)
MD_OBJS   := $(BUILD)/md_start.o $(BUILD)/md_main.o    # src/platform/32x/md_src/*

# verify_rom markers: strings that live in DIFFERENT core translation units,
# so a wrongful --gc-sections strip is caught. EDIT to strings in YOUR game.
MARKERS := --title "$(TITLE)" --marker "Press Start" --marker "Game Over"

.PHONY: all check clean
all: $(ROM)

$(BUILD) rom:
	mkdir -p $@

# ---- 68000 side: build a tiny ELF, then a raw binary to embed ----
$(BUILD)/md_start.o: src/platform/32x/md_src/md_start.s | $(BUILD)
	$(MDCC) $(MDFLAGS) -x assembler-with-cpp $< -o $@
$(BUILD)/md_main.o: src/platform/32x/md_src/md_main.c | $(BUILD)
	$(MDCC) $(MDFLAGS) $< -o $@
$(BUILD)/md_start.elf: $(MD_OBJS)
	$(MDCC) $(MDLDFLAGS) $^ -o $@ $(MDLIBS)
$(BUILD)/md_start.bin: $(BUILD)/md_start.elf
	$(MDOBJC) -O binary $< $@

# ---- SH-2 startup embeds the 68000 binary (via .incbin in mars_start.s) ----
$(BUILD)/mars_start.o: src/platform/32x/mars_start.s $(BUILD)/md_start.bin | $(BUILD)
	$(AS) --big $< -o $@

# ---- core + platform C ----
$(BUILD)/%.o: src/core/%.c | $(BUILD)
	$(CC) $(SHFLAGS) $< -o $@
$(BUILD)/%.o: src/platform/32x/%.c | $(BUILD)
	$(CC) $(SHFLAGS) $< -o $@

# ---- link, objcopy, pad, header-fix ----
$(BUILD)/$(TARGET).elf: $(SH_OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@
	$(SIZE) $@
	@grep -E 'bss_end|__stack' $(BUILD)/$(TARGET).map || true

$(ROM): $(BUILD)/$(TARGET).elf | rom
	$(OBJC) -O binary $< $(BUILD)/$(TARGET).raw
	dd if=$(BUILD)/$(TARGET).raw of=$@ bs=8192 conv=sync status=none
	python3 tools/romfix.py $@ --title "$(TITLE)"

check: $(ROM)
	python3 tests/verify_rom.py $(ROM) $(BUILD)/$(TARGET).elf $(MARKERS)
	python3 tests/run_tests.py

clean:
	rm -rf $(BUILD) rom
