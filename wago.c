/*
  This program controls I/O from/to a Wago 850 Linux controller.

  It is available under the GNU General Public license, version 3.

  Copyright © 2011 Matthias Urlichs <matthias@urlichs.de>
*/

/*
  This program is based on the sample-hello program from libevent2.
  The original is avaliable under a BSD-3 license.
*/


#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#ifndef _WIN32
#include <netinet/in.h>
# ifdef _XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
# endif
#include <sys/socket.h>
#endif
#include <getopt.h>

#include "bus.h"

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

const char *MSG_HELLO = "*WAGO ready.\n";

char debug = 
#ifdef DEMO
	1
#else
	0
#endif
		;

static int port = 59995;
static struct timeval loop_dly = {3,0};
static char *buscfg_file = NULL;

static void listener_cb(struct evconnlistener *, evutil_socket_t,
    struct sockaddr *, int socklen, void *);
static void conn_eventcb(struct bufferevent *, short, void *);
static void conn_readcb(struct bufferevent *, void *);
static void signal_cb(evutil_socket_t, short, void *);
static void timer_cb(evutil_socket_t, short, void *);
static int interface_setup(struct event_base *base, evutil_socket_t fd);

static struct event_base *base = NULL;
static struct evconnlistener *listener = NULL;
static struct event *signal_event = NULL;
static struct event *timer_event = NULL;

extern char *__progname; /* from uClibc */
static void
usage (int err)
{
	if (err != 0) {
		fprintf (stderr, "Usage: %s OPTION ...\n", __progname);
		fprintf (stderr, "Try `%s --help' for more information.\n", __progname);
	} else {
		fprintf (stdout, "\
Usage: %s OPTION ...\n\
Options:\n\
-p|--port #  Use port # instead of %d.\n\
-c|--cfg  #  Use configuration file #.\n\
-D|--debug   Toggle debugging (default %s).\n\
-l|--loop #  Check ports every # seconds instead of %g.\n\
-h|--help    Print this message.\n\
\n", __progname, port, debug?"on":"off", loop_dly.tv_sec+loop_dly.tv_usec/1000000.);
	}
	exit (err);
}

static int report_bus(struct _bus *bus, void *priv)
{
	struct evbuffer *out = (struct evbuffer *)priv;

	evbuffer_add_printf(out, "%d: %s:%s %d\n", bus->id,bus_typname(bus->typ),bus->typname, bus->bits);
	return 0;
}

static int list_bus_debug(struct _bus *bus, void *priv)
{
	printf("%d: %s:%s %d\n", bus->id,bus_typname(bus->typ),bus->typname, bus->bits);
	return 0;
}

static void
set_loop_timer(float d)
{
	loop_dly.tv_sec = (int)d;
	loop_dly.tv_usec = (int)((d-loop_dly.tv_sec)*1000000);
}

int
main(int argc, char **argv)
{
	char listen_stdin = 0;

	struct sockaddr_in sin;
#ifdef _WIN32
	WSADATA wsa_data;
#endif

	{
		int opt;
		char *ep;
		unsigned long p;
		float d;

		int option_index = 0;

		/* Defintions of all posible options */
		static struct option long_options[] = {
			{"config", 1, 0, 'c'},
			{"stdin", 0, 0, 'd'},
			{"debug", 0, 0, 'D'},
			{"help", 0, 0, 'h'},
			{"loop", 1, 0, 'l'},
			{"port", 1, 0, 'p'},
			{0, 0, 0, 0}
		};
		
		/* Identify all  options */
		while((opt= getopt_long (argc, argv, "c:dDhl:p:",
						long_options, &option_index)) >= 0) {
			switch (opt) {
			case 'd':
				listen_stdin = 1;
				break;
			case 'D':
				debug = !debug;
				break;
			case 'p':
				p = strtoul(optarg, &ep, 10);
				if(!*optarg || *ep || p>65535 || p==0 ) {
					fprintf(stderr, "'%s' is not a valid port. Port numbers need to be >0 and <65536.\n", optarg);
					exit(1);
				}
				port=p;
				break;
			case 'c':
				buscfg_file = optarg;
				break;
			case 'l':
				d = strtof(optarg, &ep);
				if(!*optarg || *ep || d>100000 || d < 0.00099) {
					fprintf(stderr, "'%s' is not a valid timer value. The timer needs to be >0.001 and <100000.\n", optarg);
					exit(1);
				}
				set_loop_timer(d);
				break;
			case 'h':
				usage(0);
				exit(0);
			default:
				return -1;
			}
		}
	}


#ifdef _WIN32
	WSAStartup(0x0201, &wsa_data);
#endif

	bus_init_data(buscfg_file);
	if (debug)
		bus_enum(list_bus_debug,NULL);

	base = event_base_new();
	if (!base) {
		fprintf(stderr, "Could not initialize libevent: %s\n",strerror(errno));
		return 1;
	}

	if (listen_stdin) {
		if (debug)
			printf("Listening on stdin.\n");
		if (interface_setup(base,fileno(stdin)) < 0) {
			fprintf(stderr, "Could not listen on stdio: %s\n",strerror(errno));
			return 1;
		}
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);

	listener = evconnlistener_new_bind(base, listener_cb, (void *)base,
	    LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
	    (struct sockaddr*)&sin,
	    sizeof(sin));

	if (!listener) {
		fprintf(stderr, "Could not create a listener: %s\n",strerror(errno));
		return 1;
	}

	signal_event = evsignal_new(base, SIGINT, signal_cb, (void *)base);
	if (!signal_event || event_add(signal_event, NULL)<0) {
		fprintf(stderr, "Could not create/add a signal event: %s\n",strerror(errno));
		return 1;
	}
	timer_event = event_new(base, -1, EV_TIMEOUT|EV_PERSIST, timer_cb, NULL);
	if (!timer_event || event_add(timer_event, &loop_dly)<0) {
		fprintf(stderr, "Could not create/add a timer event: %s\n",strerror(errno));
		return 1;
	}

	bus_sync();
	event_base_dispatch(base);
	bus_free_data();

	evconnlistener_free(listener);
	event_free(signal_event);
	event_free(timer_event);
	event_base_free(base);

	printf("done\n");
	return 0;
}

static void
listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *sa, int socklen, void *user_data)
{
	struct event_base *base = user_data;

	if(debug)
		printf("New connection: fd %d\n",fd);

	if(interface_setup(base,fd) < 0) {
		fprintf(stderr,"Could not setup interface: %s",strerror(errno));
		close(fd);
	}
}

static int
interface_setup(struct event_base *base, evutil_socket_t fd)
{
	struct bufferevent *bev;
	bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	if (bev == NULL)
		return -1;

	bufferevent_setcb(bev, conn_readcb, NULL, conn_eventcb, NULL);

	bufferevent_write(bev, MSG_HELLO, strlen(MSG_HELLO));
	bufferevent_enable(bev, EV_READ);
	return 0;
}

static const char std_help[] = "=\
Known functions:\n\
h     print help messages\n\
i A B read bit from input port A, pos B\n\
I A B report bit from output port A, pos B\n\
s A B set bit at output port A, pos B\n\
c A B clear bit at output port A, pos B\n\
d     set/query poll delay\n\
D     dump port info\n\
\n\
Send 'hX' for help on function X.\n\
.\n";
static const char std_help_h[] = "=\
h  send a generic help message, list functions\n\
hX send specific help on function X\n\
.\n";
static const char std_help_d[] = "=\
d   report current poll frequency (seconds).\n\
d X set poll frequency to X.\n\
.\n";
static const char std_help_i[] = "=\
i A B  read a bit on input port A, offset B.\n\
.\n";
static const char std_help_I[] = "=\
I A B  read the state of bit on output port A, offset B.\n\
.\n";
static const char std_help_s[] = "=\
s A B  set a bit on output port A, offset B.\n\
.\n";
static const char std_help_c[] = "=\
c A B  clear a bit on output port A, offset B.\n\
.\n";
static const char std_help_D[] = "=\
D  dump port list (human-readable version).\n\
Dp dump port list (parsed list).\n\
.\n";
static const char std_help_unknown[] = "=\
You requested help on an unknown function (%d).\n\
Send 'h' for a list of known functions.\n\
.\n";

static void
send_help(struct evbuffer *out, char h)
{
	switch(h) {
	case 0:
		evbuffer_add(out,std_help,sizeof(std_help)-1);
		break;
	case 'h':
		evbuffer_add(out,std_help_h,sizeof(std_help_h)-1);
		break;
	case 'd':
		evbuffer_add(out,std_help_d,sizeof(std_help_d)-1);
		break;
	case 'i':
		evbuffer_add(out,std_help_i,sizeof(std_help_i)-1);
		break;
	case 'I':
		evbuffer_add(out,std_help_I,sizeof(std_help_I)-1);
		break;
	case 's':
		evbuffer_add(out,std_help_s,sizeof(std_help_s)-1);
		break;
	case 'c':
		evbuffer_add(out,std_help_c,sizeof(std_help_c)-1);
		break;
	case 'D':
		evbuffer_add(out,std_help_D,sizeof(std_help_D)-1);
		break;
	default:
		evbuffer_add_printf(out,std_help_unknown, h);
		break;
	}
}

static void
parse_input(struct bufferevent *bev, const char *line)
{
	struct evbuffer *out = bufferevent_get_output(bev);
	int p1,p2;
	float p3;
	int res = 0;

	switch(*line) {
	case 'D':
		if (line[1] == 'p') {
			evbuffer_add_printf(out,"=Reporting bus data\n");
			bus_enum(report_bus, out);
			evbuffer_add(out,".\n",2);
		} else if (!line[1]) {
			FILE *fd = bus_description(); 
			int len;
			char fbuf[4096],*pbuf;
			char cont = 0;
			if (fd == NULL) {
				evbuffer_add_printf(out,"?Could not open description: %s\n",strerror(errno));
				break;
			}

			evbuffer_add_printf(out,"=port data\n");
			while(1) {
				pbuf = fgets(fbuf,sizeof(fbuf),fd);
				if (pbuf == NULL)
					break;
				len = strlen(pbuf);
				if (!cont && *pbuf == '.')
					evbuffer_add(out,".",1);
				evbuffer_add(out,pbuf,len);
				cont = pbuf[len-1] != '\n';
			}
			fclose(fd);
			evbuffer_add(out,".\n",2);
		} else {
			evbuffer_add_printf(out,"?Unknown subcommand: '%c'. Help with 'hD'.\n",line[1]);
		}
		break;
	case 'd':
		if (line[1]) {
			if(sscanf(line+1,"%g",&p3) != 1) {
				evbuffer_add_printf(out,"?d needs a float parameter.\n");
				break;
			}
			if(p3>100000 || p3 < 0.00099) {
				evbuffer_add_printf(out,"?not a valid timer parameter, 0.001 < DELAY < 10000.\n");
				break;
			}
			set_loop_timer(p3);
			if (event_del(timer_event) || event_add(timer_event, &loop_dly)<0) {
				evbuffer_add_printf(out,"?changing the timer failed: %s\n",strerror(errno));
				break;
			}
			evbuffer_add_printf(out,"+Loop timer changed.\n");
		} else {
			evbuffer_add_printf(out,"+%g seconds per loop.\n", loop_dly.tv_sec+loop_dly.tv_usec/1000000.);
		}
		break;
	case 'i':
	case 'I':
	case 's':
	case 'c':
		if(sscanf(line+1,"%d %d",&p1,&p2) != 2) {
			evbuffer_add_printf(out,"?'%c' needs two numeric parameters.\n",*line);
			break;
		}
		switch(*line) {
		case 'i':
			bus_sync();
			res = bus_read_bit(p1,p2);
			if(res < 0) {
				evbuffer_add_printf(out,"?error: %s\n",strerror(errno));
				break;
			}
			evbuffer_add_printf(out,"+%d\n",res);
			break;
		case 'I':
			res = bus_read_wbit(p1,p2);
			if(res < 0) {
				evbuffer_add_printf(out,"?error: %s\n",strerror(errno));
				break;
			}
			evbuffer_add_printf(out,"+%d\n",res);
			break;
		case 's':
			res = bus_write_bit(p1,p2,1);
			if(res < 0) {
				evbuffer_add_printf(out,"?error: %s\n",strerror(errno));
				break;
			}
			bus_sync();
			evbuffer_add_printf(out,"+Set.\n");
			break;
		case 'c':
			res = bus_write_bit(p1,p2,0);
			if(res < 0) {
				evbuffer_add_printf(out,"?error: %s\n",strerror(errno));
				break;
			}
			bus_sync();
			evbuffer_add_printf(out,"+Cleared.\n");
			break;
		}
		break;
	case 'h':
		send_help(out,line[1]);
		break;
	case 0:
		evbuffer_add_printf(out,"?Empty line. Help with 'h'.\n");
		break;
	default:
		evbuffer_add_printf(out,"?Unknown character: '%c'. Help with 'h'.\n",*line);
		break;
	}
}

static void
conn_readcb(struct bufferevent *bev, void *user_data)
{
	struct evbuffer *buf = bufferevent_get_input(bev);
	while(1) {
		char *line;
		size_t len;

		line = evbuffer_readln(buf, &len, EVBUFFER_EOL_CRLF);
		if (line == NULL)
			break;
		if(debug)
			printf("Read on %d: %s.\n", bufferevent_getfd(bev),line);
		parse_input(bev,line);
		free(line);
	}
}

static void
conn_eventcb(struct bufferevent *bev, short events, void *user_data)
{
	if (events & BEV_EVENT_EOF) {
		printf("Connection %d closed.\n", bufferevent_getfd(bev));
	} else if (events & BEV_EVENT_ERROR) {
		printf("Got an error on connection %d: %s\n", bufferevent_getfd(bev), strerror(errno));
	}
	/* None of the other events can happen here, since we haven't enabled
	 * timeouts */
	bufferevent_free(bev);
}

static void
signal_cb(evutil_socket_t sig, short events, void *user_data)
{
	struct event_base *base = user_data;
	struct timeval delay = { 2, 0 };

	printf("Caught an interrupt signal; exiting cleanly in two seconds.\n");

	event_base_loopexit(base, &delay);
}

static void
timer_cb(evutil_socket_t sig, short events, void *user_data)
{
	if(debug)
		printf("Loop.\n");
	bus_sync();
}

