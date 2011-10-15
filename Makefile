#!/usr/bin/make -f

export ACC=arm-uclinux-elf-gcc
export PATH=/usr/local/bin:/usr/bin:/bin

SRC := $(wildcard *.c)

CFLAGS=-O2 -g
ACFLAGS=-Os -g -I/usr/local/arm-linux-uclibc/include/

LDFLAGS=
ALDFLAGS=-Wl,-elf2flt -L/usr/local/arm-linux-uclibc/lib/

OBJ := $(addsuffix .o,$(basename $(SRC)))
WOBJ := $(addsuffix .ao,$(basename $(SRC)))
LIBS=-levent

all: wago.bflt wago

%.ao: %.c
	$(ACC) $(ACFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) -DDEBUG $(CFLAGS) -c -o $@ $<

wago: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

wago.bflt: $(WOBJ)
	$(ACC) $(ALDFLAGS) -o $@ $^ $(LIBS)

wago: $(OBJ)

clean:
	rm -f $(OBJ) $(WOBJ)
	rm -f wago.bflt wago
