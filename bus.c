
#include "wago.h"
#include "bus.h"
#include "kbusapi.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

extern char debug;

// cat /proc/driver/kbus/config.csv 
//line      ??      ??      rboff   woffset wwidth
//  name        bits    roffset rwidth  wboff
//1	750-4xx	0	8	n	0	0	0	0	0	8
//1	750-5xx	0	16	n	0	0	16	0	0	0
//2	750-5xx	0	16	n	2	0	16	0	0	0

struct _bus_priv;
struct _bus_priv {
	struct _bus bus;
	struct _bus_priv *next;
	unsigned char byte_offset, bit_offset;
};
static struct _bus_priv *bus_list = NULL;
static const char *bus_file = NULL;

/* Initialize/free data */
int bus_init_data(const char *fn)
{
	FILE *f;
	int res = 0;
#ifdef DEMO
	if(fn == NULL)
		return 0;
	bus_file = strdup(fn);
#else
	if(fn == NULL) {
		fn = "/proc/driver/kbus/config.csv";
		bus_file = "/proc/driver/kbus/config";
	}

	KbusOpen();
#endif

	f = fopen(fn,"r");
	if (f == NULL)
		return -1;
	while(1) {
		struct _bus_priv *dev;
		char devtyp[BUS_TYPNAME_LEN],x3[3];
		int i,x1,bits;
		int roff,rboff,rsiz;
		int woff,wboff,wsiz;
		int len = fscanf(f,"%d %10s %d %d %2s %d %d %d %d %d %d\n",
			&i,devtyp,&x1,&bits,x3, &woff,&wboff,&wsiz, &roff,&rboff,&rsiz);
		if (len == 0)
			break;
		if (len != 11) {
			res = -1;
			break;
		}
		dev = malloc(sizeof(*dev));
		if (dev == NULL) {
			res = -2;
			break;
		}
		memset(dev,0,sizeof(*dev));
		dev->bus.id = i;
		strcpy(dev->bus.typname,devtyp);

		if(!strcmp(devtyp,"750-5xx")) { // digital output
			dev->bus.typ = BUS_BITS_OUT;
			dev->byte_offset = woff;
			dev->bit_offset = wboff;
			dev->bus.bits = wsiz;
		} else if(!strcmp(devtyp,"750-4xx")) { // digital output
			dev->bus.typ = BUS_BITS_IN;
			dev->byte_offset = roff;
			dev->bit_offset = rboff;
			dev->bus.bits = rsiz;
		} else {
			dev->bus.typ = BUS_UNKNOWN;
		}
		dev->next = bus_list;
		bus_list = dev;
	}
	fclose(f);
	return res;
}

void bus_free_data()
{
#ifndef DEMO
	KbusClose();
#endif
}

/* return a file with data describing the bus */
FILE *bus_description(void)
{
	if (bus_file == NULL)
		bus_file = "/dev/null";
	return fopen(bus_file,"r");
}


/* Enumerate the bus. Return something != 0 to break the enumerator loop. */
// int (*bus_enum_fn)(struct _bus *bus, void *priv);
int bus_enum(bus_enum_fn enum_fn, void *priv)
{
	struct _bus_priv *bus;
	int res = 0;
	for(bus = bus_list; bus; bus = bus->next) {
		res = (*enum_fn)(&bus->bus, priv);
		if (res)
			break;
	}
	return res;
}

const char *bus_typname(enum bus_type typ)
{
	switch(typ) {
	case BUS_UNKNOWN:
		return "unknown device";
	case BUS_BITS_IN:
		return "digital input";
	case BUS_BITS_OUT:
		return "digital output";
	default:
		return "???";
	}
}

/* sync bus state */
void bus_sync()
{
#ifndef DEMO
	KbusUpdate();
#endif
}


/*
   Check if this bit is on the bus.
   Input: Slot number and -based bit position.
   Output: Byte and bit offset for hardware access.
   Returns: -1 if invalid oarameters, else 0.
 */
int _bus_find_bit(unsigned short *_port,unsigned short *_offset, enum bus_type typ)
{
	struct _bus_priv *bus;
	unsigned short port = *_port;
	unsigned short offset = *_offset;

	for(bus = bus_list; bus; bus = bus->next) {
		if (bus->bus.id != port)
			continue;

		if (bus->bus.typ != typ) {
			errno = EINVAL;
			if(debug) printf("Check %d %d for %s FAILED: wrong device\n",port,offset,bus_typname(typ));
			return -1;
		}

		if (offset == 0 || offset > bus->bus.bits) {
			errno = EINVAL;
			if(debug) printf("Check %d %d for %s FAILED: max %d bits\n",port,offset,bus_typname(typ), bus->bus.bits);
			return -1;
		}

		offset += bus->bit_offset-1;
		*_port = bus->byte_offset + (offset>>3);
		*_offset = offset & 7;
		return 0;
	}
		
	if(debug) printf("Check %d %d for %s FAILED: ID not found\n",port,offset,bus_typname(typ));
	errno = ENODEV;
	return -1;
}

int bus_is_read_bit(unsigned short *port,unsigned short *offset)
{
	return _bus_find_bit(port,offset,BUS_BITS_IN);
}

int bus_is_write_bit(unsigned short *port,unsigned short *offset)
{
	return _bus_find_bit(port,offset,BUS_BITS_OUT);
}


/* read a bit, or return a bit's write status */
char _bus_read_bit(unsigned short port,unsigned short offset)
{
	char res = 0;
#ifdef DEMO
	res = (rand() < RAND_MAX/10);
	if (res)
		res = 1-demo_state_r;
	else
		res = demo_state_r;
#else
	res = (pstPabIN->uc.Pab[port] & (1<<offset)) ? 1 : 0;
#endif
	if(debug)
		printf("    bit %d:%d = %d\n", port,offset, res);
	return res;
}

char _bus_read_wbit(unsigned short port,unsigned short offset)
{
	char res = 0;
#ifdef DEMO
	res = (rand() < RAND_MAX/10);
	if (res)
		res = 1-demo_state_w;
	else
		res = demo_state_w;
#else
	res = (pstPabOUT->uc.Pab[port] & (1<<offset)) ? 1 : 0;
#endif
	if(debug)
		printf("   wbit %d:%d = %d\n", port,offset, res);
	return res;
}


/* write a bit */
void _bus_write_bit(unsigned short port,unsigned short offset, char value)
{
	if(debug)
		printf("Set bit %d:%d = %d\n", port,offset, value);
#ifdef DEMO
	demo_state_w = value;
#else
	if (value) {
		pstPabOUT->uc.Pab[port] |= 1<<offset;
	} else {
		pstPabOUT->uc.Pab[port] &= ~(1<<offset);
	}
#endif
}

