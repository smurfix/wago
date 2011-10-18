
#include "wago.h"
#include "mon.h"
#include "bus.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <event2/event.h>
#include <event2/buffer.h>

//struct _mon {
//	enum mon_type typ;
//	unsigned int id;
//	unsigned char port,offset;
//};
struct _mon_priv;
struct _mon_priv {
	struct _mon mon;
	struct _mon_priv *next;
	struct bufferevent *buf;
	struct event *timer;
	struct timeval report_dly;
	unsigned short _port,_offset;
	unsigned long count;
	unsigned char state;
};
static struct _mon_priv *mon_list = NULL;
static int last_mon_id = 0;

int mon_new(enum mon_type typ, unsigned char port, unsigned char offset, struct bufferevent *buf, unsigned int msec)
{
	struct _mon_priv *mon;
	unsigned short _port = port;
	unsigned short _offset = offset;

	if (bus_is_read_bit(&_port,&_offset) < 0)
		return -1;

	mon = malloc(sizeof(*mon));
	if (mon == NULL)
		return -1;
	memset(mon,0,sizeof(*mon));
	mon->mon.id = ++last_mon_id;
	mon->mon.typ = typ;
	mon->mon.port = port;
	mon->_port = _port;
	mon->mon.offset = offset;
	mon->_offset = _offset;
	mon->state = _bus_read_bit(_port,_offset);
	mon->buf = buf;
	mon->report_dly.tv_sec = msec/1000;
	mon->report_dly.tv_usec = 1000*(msec-1000*mon->report_dly.tv_sec);

	mon->next = mon_list;
	mon_list = mon;
	if(debug)
		printf("New Monitor %s:%d: %d:%d > %d:%d %d\n",
			mon_typname(mon->mon.typ),mon->mon.id, port,offset, _port,_offset, mon->state);
	return mon->mon.id;
}

static void mon_free(struct _mon_priv *mon)
{
	if (mon->timer)
		event_del(mon->timer);
	free(mon);
}

int mon_del(int id)
{
	struct _mon_priv **pmon = &mon_list;
	while(1) {
		struct _mon_priv *mon = *pmon;
		if (mon == NULL) {
			errno = -ENOENT;
			return -1;
		}
		if (mon->mon.id == id) {
			*pmon = mon->next;
			mon_free(mon);
			return 0;
		}
		pmon = &mon->next;
	}
}

void mon_delbuf(struct bufferevent *buf)
{
	struct _mon_priv **pmon = &mon_list;
	while(1) {
		struct _mon_priv *mon = *pmon;
		if (mon == NULL)
			return;
		if (mon->buf == buf) {
			*pmon = mon->next;
			mon_free(mon);
		}
		pmon = &mon->next;
	}
}


/* Enumerate the monitors. Return something != 0 to break the enumerator loop. */
//typedef int (*mon_enum_fn)(struct _mon *mon, void *priv);
int mon_enum(mon_enum_fn enum_fn, void *priv)
{
	struct _mon_priv *mon;
	int res = 0;
	for(mon = mon_list; mon; mon = mon->next) {
		res = (*enum_fn)(&mon->mon, priv);
		if (res)
			break;
	}
	return res;
}

const char *mon_typname(enum mon_type typ)
{
	switch(typ) {
	case MON_UNKNOWN:
		return "unknown monitor";
	case MON_REPORT:
		return "report change";
	case MON_REPORT_H:
		return "report change h";
	case MON_REPORT_L:
		return "report change l";
	case MON_COUNT:
		return "count changes";
	case MON_COUNT_H:
		return "count changes h";
	case MON_COUNT_L:
		return "count changes l";
	default:
		return "???";
	}
}

static void
timer_cb(evutil_socket_t sig, short events, void *user_data)
{
	struct _mon_priv *mon = (struct _mon_priv *)user_data;
	struct evbuffer *out = bufferevent_get_output(mon->buf);

	mon->timer = NULL;
	evbuffer_add_printf(out, "*m%d %ld\n", mon->mon.id, mon->count);
}

/* check monitor state */
void mon_sync(void)
{
	struct _mon_priv *mon;

	for(mon = mon_list; mon; mon = mon->next) {
		unsigned char state = _bus_read_bit(mon->_port,mon->_offset);
		struct evbuffer *out = bufferevent_get_output(mon->buf);
		if(!state == !mon->state)
			continue;
		mon->state = state;
		switch(mon->mon.typ) {

		case MON_REPORT:
		mon_report:
			if(debug)
				printf("Mon%d: %c\n", mon->mon.id, state?'H':'L');
			evbuffer_add_printf(out, "*m%d %c\n", mon->mon.id, state?'H':'L');
			break;
		case MON_REPORT_H:
			if(!state) continue;
			goto mon_report;
		case MON_REPORT_L:
			if(state) continue;
			goto mon_report;

		case MON_COUNT:
		mon_count:
			mon->count++;
			if (mon->timer == NULL) {
				if(debug)
					printf("Mon%d: %ld %ld.%06lu\n", mon->mon.id, mon->count, mon->report_dly.tv_sec,mon->report_dly.tv_usec);
				mon->timer = event_new(base, -1, EV_TIMEOUT, timer_cb, mon);
				if(mon->timer == NULL || event_add(mon->timer, &mon->report_dly)) {
					evbuffer_add_printf(out, "*m%d %ld\n", mon->mon.id, mon->count);
					evbuffer_add_printf(out, "* Create monitor timeout: %s\n", strerror(errno));
				}
			} else {
				if(debug)
					printf("Mon%d: %ld\n", mon->mon.id, mon->count);
			}
			break;
		case MON_COUNT_H:
			if(!state) continue;
			goto mon_count;
		case MON_COUNT_L:
			if(state) continue;
			goto mon_count;

		default:
			continue;
		}
	}
}

