
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
	struct timeval delay;
	struct timeval delay2;
	unsigned short _port,_offset;
	unsigned long count;
	unsigned char state;
};
static struct _mon_priv *mon_list = NULL;
static int last_mon_id = 0;

static void counter_cb(evutil_socket_t sig, short events, void *user_data);
static void once_cb(evutil_socket_t sig, short events, void *user_data);
static void loop_cb(evutil_socket_t sig, short events, void *user_data);

int mon_new(enum mon_type typ, unsigned char port, unsigned char offset, struct bufferevent *buf,
	unsigned int msec, unsigned int msec2)
{
	struct _mon_priv *mon;
	unsigned short _port = port;
	unsigned short _offset = offset;
	unsigned char state;

	if (typ > _MON_UNKNOWN_OUT) {
		if (bus_is_write_bit(&_port,&_offset) < 0)
			return -1;
		state = _bus_read_wbit(_port,_offset);

		if (state) {
			if (typ == MON_SET_ONCE || typ == MON_SET_LOOP) {
				errno = EEXIST;
				return -1;
			}
		} else {
			if (typ == MON_CLEAR_ONCE || typ == MON_CLEAR_LOOP) {
				errno = EEXIST;
				return -1;
			}
		}
	} else {
		if (bus_is_read_bit(&_port,&_offset) < 0)
			return -1;
		state = _bus_read_bit(_port,_offset);
	}

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
	mon->state = state;
	mon->buf = buf;
	mon->delay.tv_sec = msec/1000;
	mon->delay.tv_usec = 1000*(msec-1000*mon->delay.tv_sec);
	if(typ > _MON_UNKNOWN_OUT) {
		switch(typ) {
		case MON_SET_LOOP:
		case MON_CLEAR_LOOP:
			mon->delay2.tv_sec = msec2/1000;
			mon->delay2.tv_usec = 1000*(msec2-1000*mon->delay2.tv_sec);
			mon->timer = event_new(base, -1, EV_TIMEOUT, loop_cb, mon);
			break;
		case MON_SET_ONCE:
		case MON_CLEAR_ONCE:
			mon->timer = event_new(base, -1, EV_TIMEOUT, once_cb, mon);
			break;
		default:
			break;
		}
		if(mon->timer == NULL || event_add(mon->timer, &mon->delay)) {
			if(mon->timer) event_free(mon->timer);
			free(mon);
			return -1;
		}
		switch(typ) {
		case MON_SET_LOOP:
		case MON_SET_ONCE:
			_bus_write_bit(_port,_offset, 1);
			mon->state = 1;
			break;
		case MON_CLEAR_LOOP:
		case MON_CLEAR_ONCE:
			_bus_write_bit(_port,_offset, 0);
			mon->state = 0;
			break;
		default:
			break;
		}
		bus_sync();
	}

	mon->next = mon_list;
	mon_list = mon;
	if(debug)
		printf("New Monitor %s:%d: %d:%d > %d:%d %d\n",
			mon_typname(mon->mon.typ),mon->mon.id, port,offset, _port,_offset, mon->state);
	return mon->mon.id;
}

static void mon_free(struct _mon_priv *mon, struct bufferevent *buf)
{
	if (mon->timer) {
		event_del(mon->timer);
		event_free(mon->timer);
	}
	if(mon->buf != NULL && mon->buf != buf) {
		struct evbuffer *out = bufferevent_get_output(mon->buf);
		evbuffer_add_printf(out, "!-%d Deleted.\n", mon->mon.id);
	}

	free(mon);
}

int mon_del(int id, struct bufferevent *buf)
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
			mon_free(mon,buf);
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
			mon_free(mon,buf);
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
	/* read ports */
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

	/* write ports */
	case MON_SET_ONCE:
		return "timed set";
	case MON_SET_LOOP:
		return "PWM, on";
	case MON_CLEAR_ONCE:
		return "timed clear";
	case MON_CLEAR_LOOP:
		return "PWM, off";
	default:
		return "???";
	}
}

static void
counter_cb(evutil_socket_t sig, short events, void *user_data)
{
	struct _mon_priv *mon = (struct _mon_priv *)user_data;
	struct evbuffer *out = bufferevent_get_output(mon->buf);

	event_free(mon->timer);
	mon->timer = NULL;
	evbuffer_add_printf(out, "!%d %ld\n", mon->mon.id, mon->count);
}

static void
once_cb(evutil_socket_t sig, short events, void *user_data)
{
	struct _mon_priv *mon = (struct _mon_priv *)user_data;
	struct evbuffer *out = bufferevent_get_output(mon->buf);

	if(debug)
		printf("monitor %d triggers\n", mon->mon.id);
	if (_bus_read_wbit(mon->_port,mon->_offset) == mon->state) {
		_bus_write_bit(mon->_port,mon->_offset, !mon->state);
		evbuffer_add_printf(out, "!%d TRIGGER\n", mon->mon.id);
		bus_sync();
	} else {
		evbuffer_add_printf(out, "!-%d Already changed!\n", mon->mon.id);
		mon->buf = NULL;
	}

	event_free(mon->timer);
	mon->timer = NULL;

	mon_del(mon->mon.id, NULL);
}

static void
loop_cb(evutil_socket_t sig, short events, void *user_data)
{
	struct _mon_priv *mon = (struct _mon_priv *)user_data;
	struct evbuffer *out = bufferevent_get_output(mon->buf);
	struct timeval tv;

	if(_bus_read_wbit(mon->_port,mon->_offset) == mon->state) {
		if(mon->mon.typ == MON_CLEAR_LOOP) {
			mon->mon.typ = MON_SET_LOOP;
			mon->state = 1;
		} else {
			mon->mon.typ = MON_CLEAR_LOOP;
			mon->state = 0;
		}
		_bus_write_bit(mon->_port,mon->_offset, mon->state);
	
		if(debug)
			printf("monitor %d toggles: %c\n", mon->mon.id, mon->state ? 'H' : 'L');
	
		tv = mon->delay;
		mon->delay = mon->delay2;
		mon->delay2 = tv;
		event_add(mon->timer, &mon->delay);

	} else {
		event_free(mon->timer);
		mon->timer = NULL;

		evbuffer_add_printf(out, "!-%d DROP: saw external change in timer\n", mon->mon.id);

		mon->buf = NULL;
		mon_del(mon->mon.id, NULL);
	}
}

/* check monitor state */
void mon_sync(void)
{
	struct _mon_priv *mon,*mon2;

	for(mon = mon_list; mon; mon = mon2) {
		unsigned char state;
		struct evbuffer *out = bufferevent_get_output(mon->buf);

		mon2 = mon->next;

		if (mon->mon.typ > _MON_UNKNOWN_OUT)
			state = _bus_read_wbit(mon->_port,mon->_offset);
		else
			state = _bus_read_bit(mon->_port,mon->_offset);
		if(!state == !mon->state)
			continue;

		mon->state = state;
		switch(mon->mon.typ) {
		/* Outputs: if the output has been changed externally, die. */
		case MON_SET_ONCE:
		case MON_SET_LOOP:
			_bus_write_bit(mon->_port,mon->_offset, 0);
			goto clear_common;
		case MON_CLEAR_ONCE:
		case MON_CLEAR_LOOP:
			_bus_write_bit(mon->_port,mon->_offset, 1);
		clear_common:
			if(debug)
				printf("Mon%d: dropped, found %c\n", mon->mon.id, state?'H':'L');
			evbuffer_add_printf(out, "!-%d DROP %c: saw external change in loop\n", mon->mon.id, state?'H':'L');
			mon->buf = NULL;
			mon_del(mon->mon.id, NULL);
			break;
			
		/* Inputs: change reporting */
		case MON_REPORT:
		mon_report:
			if(debug)
				printf("Mon%d: %c\n", mon->mon.id, state?'H':'L');
			evbuffer_add_printf(out, "!%d %c\n", mon->mon.id, state?'H':'L');
			break;
		case MON_REPORT_H:
			if(!state) continue;
			goto mon_report;
		case MON_REPORT_L:
			if(state) continue;
			goto mon_report;

		/* Inputs: change counting */
		case MON_COUNT:
		mon_count:
			mon->count++;
			if (mon->timer == NULL) {
				if(debug)
					printf("Mon%d: %ld %ld.%06lu\n", mon->mon.id, mon->count, mon->delay.tv_sec,mon->delay.tv_usec);
				mon->timer = event_new(base, -1, EV_TIMEOUT, counter_cb, mon);
				if(mon->timer == NULL || event_add(mon->timer, &mon->delay)) {
					evbuffer_add_printf(out, "!%d %ld\n", mon->mon.id, mon->count);
					evbuffer_add_printf(out, "* Monitor timeout: error: %s\n", strerror(errno));
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

