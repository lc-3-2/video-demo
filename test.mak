# Makefile for the test harness
#
# See: test.c

CC = cc
CFLAGS = \
	-DDECODER_VALIDATE \
	-g -O2 \
	-Wall -Wextra
LDFLAGS =
LFLAGS =

EFILE = video-demo-test
OFILES = test.o decoder.o

.PHONY: all
all: $(EFILE)

.PHONY: clean
clean:
	rm -fv $(EFILE) $(OFILES)

$(EFILE): $(OFILES)
	$(CC) $(LDFLAGS) -o $@ $^ $(LFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^
