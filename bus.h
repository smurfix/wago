#ifndef BUS_H
#define BUS_H

#include <stdio.h>

/* Bus descriptor */
enum bus_type {
	BUS_UNKNOWN,
	BUS_BITS_IN,
	BUS_BITS_OUT,
};
#define BUS_TYPNAME_LEN 10
struct _bus {
	enum bus_type typ;
	unsigned char id;
	unsigned char bits;
	char typname[BUS_TYPNAME_LEN];
};

/* Initialize/free data */
int bus_init_data(const char *fn);
void bus_free_data();

/* return a file with data describing the bus */
FILE *bus_description(void);

/* Enumerate the bus. Return something != 0 to break the enumerator loop. */
typedef int (*bus_enum_fn)(struct _bus *bus, void *priv);
int bus_enum(bus_enum_fn, void *priv);
const char *bus_typname(enum bus_type typ);

/* sync bus state */
void bus_sync(void);

/* check if this bit is on the bus for reading/writing.
   This may modify its input values, so call exactly once.
 */
int bus_is_read_bit(unsigned short *port,unsigned short *offset);
int bus_is_write_bit(unsigned short *port,unsigned short *offset);

/* read a bit, or return a bit's write status */
/* The macro version checks for validity: for use in one-off accesses. */
/* The function version does not check for validity: check manually; for use in timer loops. */
char _bus_read_bit(unsigned short port,unsigned short offset);
char _bus_read_wbit(unsigned short port,unsigned short offset);
#define bus_read_bit(_p,_o) ({\
		unsigned short p = (_p); \
		unsigned short o = (_o); \
		((bus_is_read_bit(&p,&o) == 0) ? _bus_read_bit(p,o) : -1); \
	})
#define bus_read_wbit(_p,_o) ({\
		unsigned short p = (_p); \
		unsigned short o = (_o); \
		((bus_is_write_bit(&p,&o) == 0) ? _bus_read_wbit(p,o) : -1); \
	})

/* write a bit */
void _bus_write_bit(unsigned short port,unsigned short offset, char value);
#define bus_write_bit(_p,_o,_v) ({ \
		unsigned short p = (_p); \
		unsigned short o = (_o); \
		((bus_is_write_bit(&p,&o) == 0) ? _bus_write_bit(p,o,(_v)),0 : -1); \
	})

#endif
