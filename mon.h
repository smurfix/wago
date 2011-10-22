#ifndef MON_H
#define MON_H

#include <event2/bufferevent.h>

/* Monitor descriptor */
enum mon_type {
	MON_UNKNOWN,

	/* report input changes */
	MON_REPORT,
	MON_REPORT_H,
	MON_REPORT_L,

	/* count input changes */
	MON_COUNT,
	MON_COUNT_H,
	MON_COUNT_L,

	/* Marker; above are inputs, below are outputs */
	_MON_UNKNOWN_OUT,

	/* set for a pre-determined time */
	MON_SET_ONCE,
	MON_CLEAR_ONCE,

	/* switch between set and clear */
	MON_SET_LOOP,
	MON_CLEAR_LOOP,
};

struct _mon {
	enum mon_type typ;
	unsigned int id;
	unsigned char port,offset;
};

int mon_new(enum mon_type typ, unsigned char port, unsigned char offset, struct bufferevent *buf,
	unsigned int msec1, unsigned int msec2);
int mon_del(int id);
void mon_delbuf(struct bufferevent *buf);

/* Enumerate the monitors. Return something != 0 to break the enumerator loop. */
typedef int (*mon_enum_fn)(struct _mon *mon, void *priv);
int mon_enum(mon_enum_fn, void *priv);
const char *mon_typname(enum mon_type typ);

/* check monitor state */
void mon_sync(void);

#endif
