#ifndef BUS_H
#define BUS_H

/* Initialize/free data */
int bus_init_data(const char *fn);
void bus_free_data();

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
