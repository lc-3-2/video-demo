# Makefile for the main program

ifndef VIDEO_DEMO_CVID
$(error "Define VIDEO_DEMO_CVID to the raw cinepak data to use")
endif

CC = clang
AS = llvm-mc
XXD = xxd
MKTEMP = mktemp

CDEFS =

ASFLAGS = --arch=lc-3.2 --filetype=obj
CFLAGS = \
	$(CDEFS) \
	--target=lc_3.2 -g -O2 -Wall -Wextra \
	-mllvm -lc_3.2-use-r4 -mllvm -lc_3.2-use-r7 \
	-mllvm -verify-machineinstrs
LDFLAGS = -nostdlib -Wl,--gc-sections
LFLAGS = -T ldscript

EFILE = video-demo.elf
OFILES = startup.o main.o decoder.o video.o

.PHONY: all
all: $(EFILE)

.PHONY: clean
clean:
	rm -fv $(EFILE) $(OFILES) video.cvid video.c

$(EFILE): $(OFILES)
	$(CC) $(LDFLAGS) -o $@ $^ $(LFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $^

video.c: video.cvid
	$(XXD) -i $^ > $@

video.cvid:
	cp "$(VIDEO_DEMO_CVID)" video.cvid
