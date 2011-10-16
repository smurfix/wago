#ifndef BUS_H
#define BUS_H

/* Bus descriptor */
enum bus_type {
	BUS_UNKNOWN,
	BUS_BITS_IN,
	BUS_BITS_OUT,
};
#define BUS_TYPNAME_LEN 10
struct _bus {
	enum bus_type typ;
	unsigned char id, byte_offset, bit_offset, bits;
	char typname[BUS_TYPNAME_LEN];
};

/* Initialize/free data */
int bus_init_data(const char *fn);
void bus_free_data();

/* Enumerate the bus. Return something != 0 to break the enumerator loop. */
typedef int (*bus_enum_fn)(struct _bus *bus, void *priv);
int bus_enum(bus_enum_fn, void *priv);
const char *bus_typname(enum bus_type typ);

/* sync bus state */
void bus_sync();

/* check if this bit is on the bus for reading/writing */
int bus_is_read_bit(short port,short offset);
int bus_is_write_bit(short port,short offset);

/* read a bit, or return a bit's write status */
char bus_read_bit(short port,short offset);
char bus_read_wbit(short port,short offset);

/* write a bit */
void bus_write_bit(short port,short offset, char value);

#endif
