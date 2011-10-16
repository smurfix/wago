
#include "bus.h"

// cat /proc/driver/kbus/config.csv 
// line        ??      ??      ??      ??      wwidth
//    name        bits    offset  rwidth  ??
//1	750-4xx	0	8	n	0	0	0	0	0	8
//1	750-5xx	0	16	n	0	0	16	0	0	0
//2	750-5xx	0	16	n	2	0	16	0	0	0

/* Initialize/free data */
int bus_init_data(const char *fn)
{
	return -1;
}

void bus_free_data()
{
}


/* sync bus state */
void bus_sync()
{
}


/* check if this bit is on the bus for reading/writing */
int bus_is_read_bit(short port,short offset)
{
	return -1;
}

int bus_is_write_bit(short port,short offset)
{
	return -1;
}


/* read a bit, or return a bit's write status */
char bus_read_bit(short port,short offset)
{
	return -1;
}

char bus_read_wbit(short port,short offset)
{
	return -1;
}


/* write a bit */
void bus_write_bit(short port,short offset, char value)
{
}

