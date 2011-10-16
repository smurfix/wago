
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
};
static struct _bus_priv *bus_list = NULL;

/* Initialize/free data */
int bus_init_data(const char *fn)
{
	FILE *f;
	int res = 0;
#ifdef DEMO
	if(fn == NULL)
		return 0;
#else
	if(fn == NULL)
		fn = "/proc/driver/kbus/config.csv";

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
			&i,devtyp,&x1,&bits,x3, &roff,&rboff,&rsiz, &woff,&wboff,&wsiz);
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

		if(!strcmp(devtyp,"750-4xx")) { // digital output
			dev->bus.typ = BUS_BITS_OUT;
			dev->bus.byte_offset = woff;
			dev->bus.bit_offset = wboff;
			dev->bus.bits = wsiz;
		} else if(!strcmp(devtyp,"750-5xx")) { // digital output
			dev->bus.typ = BUS_BITS_IN;
			dev->bus.byte_offset = roff;
			dev->bus.bit_offset = rboff;
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


/* check if this bit is on the bus for reading/writing */
int _bus_is_rw_bit(short *_port,short *_offset, enum bus_type typ)
{
	struct _bus_priv *bus;
	short port = *_port;
	short offset = *_offset;

	for(bus = bus_list; bus; bus = bus->next) {
		if (bus->bus.typ != typ)
			continue;

		if (port != bus->bus.byte_offset)
			continue;

		if (offset < bus->bus.bit_offset)
			continue;
		if (offset >= bus->bus.bit_offset+bus->bus.bits)
			continue;

		if (offset > 7) {
			*_port = port + (offset>>3);
			*_offset = offset & 7;
		}
		return 0;
	}
		
	errno = -EINVAL;
	return -1;
}

int bus_is_read_bit(short *port,short *offset)
{
	return _bus_is_rw_bit(port,offset,BUS_BITS_IN);
}
int bus_is_write_bit(short *port,short *offset)
{
	return _bus_is_rw_bit(port,offset,BUS_BITS_OUT);
}


/* read a bit, or return a bit's write status */
char _bus_read_bit(short port,short offset)
{
	char res = 0;
#ifndef DEMO
	res = (pstPabIN->uc.Pab[port] & (1<<offset)) ? 1 : 0;
#endif
	if(debug)
		printf("    bit %d:%d = %d\n", port,offset, res);
	return res;
}

char _bus_read_wbit(short port,short offset)
{
	char res = 0;
#ifndef DEMO
	res = (pstPabOUT->uc.Pab[port] & (1<<offset)) ? 1 : 0;
#endif
	if(debug)
		printf("   wbit %d:%d = %d\n", port,offset, res);
	return res;
}


/* write a bit */
void _bus_write_bit(short port,short offset, char value)
{
	if(debug)
		printf("Set bit %d:%d = %d\n", port,offset, value);
#ifndef DEMO
	if (value) {
		pstPabOUT->uc.Pab[port] |= 1<<offset;
	} else {
		pstPabOUT->uc.Pab[port] &= ~(1<<offset);
	}
#endif
}

