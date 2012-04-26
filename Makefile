#!/usr/bin/make -f

export ACC=arm-uclinux-elf-gcc
export PATH=/usr/local/bin:/usr/bin:/bin

SRC := $(wildcard *.c)

CFLAGS=-O2 -g -Wall -Werror -DDEMO -D_ISOC99_SOURCE
ACFLAGS=-Os -g -Wall -Werror        -D_ISOC99_SOURCE -I/usr/local/arm-linux-uclibc/include/ 

LDFLAGS=
ALDFLAGS=-Wl,-elf2flt -L/usr/local/arm-linux-uclibc/lib/

OBJ := $(addsuffix .o,$(basename $(SRC)))
WOBJ := $(addsuffix .ao,$(basename $(SRC)))
LIBS=-levent

all: wago.bflt wago

install:
	install wago $(ROOT)/usr/lib/homevent/
	install wago.bflt $(ROOT)/usr/lib/homevent/

w: wago.bflt
	-ncftpput -uroot -Y"CHMOD 755 wago.bflt" wago1 . wago.bflt
	#-ncftpput -uroot -Y"CHMOD 755 wago.bflt" wago2 . wago.bflt
	#-ncftpput -uroot -Y"CHMOD 755 wago.bflt" wago3 . wago.bflt

%.ao: %.c *.h
	$(ACC) $(ACFLAGS) -c -o $@ $<

%.o: %.c *.h
	$(CC) -DDEBUG $(CFLAGS) -c -o $@ $<

wago: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

wago.bflt: $(WOBJ)
	$(ACC) $(ALDFLAGS) -o $@ $^ $(LIBS)

wago: $(OBJ)

clean:
	rm -f $(OBJ) $(WOBJ)
	rm -f wago.bflt wago
