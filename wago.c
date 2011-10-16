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
#include <signal.h>
#ifndef _WIN32
#include <netinet/in.h>
# ifdef _XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
# endif
#include <sys/socket.h>
#endif
#include <getopt.h>

#ifndef DEMO
#include "kbusapi.h"
#endif

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

static const char MESSAGE[] = "Hello, World!\n";

static int port = 59995;
static struct timeval loop_dly = {3,0};

static void listener_cb(struct evconnlistener *, evutil_socket_t,
    struct sockaddr *, int socklen, void *);
static void conn_eventcb(struct bufferevent *, short, void *);
static void signal_cb(evutil_socket_t, short, void *);
static void timer_cb(evutil_socket_t, short, void *);

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
-l|--loop #  Check ports every # seconds instead of %g.\n\
-h|--help    Print this message.\n\
\n", __progname, port, loop_dly.tv_sec+loop_dly.tv_usec/1000000.);
	}
	exit (err);
}

int
main(int argc, char **argv)
{
	struct event_base *base;
	struct evconnlistener *listener;
	struct event *signal_event;
	struct event *timer_event;

	struct sockaddr_in sin;
#ifdef _WIN32
	WSADATA wsa_data;
#endif

	{
		int opt;
		char *ep;
		unsigned long p;
		double d;

		int option_index = 0;

		/* Defintions of all posible options */
		static struct option long_options[] = {
			{"port", 1, 0, 'p'},
			{"help", 0, 0, 'h'},
			{"loop", 1, 0, 'l'},
			{0, 0, 0, 0}
		};
		
		/* Identify all  options */
		while((opt= getopt_long (argc, argv, "hp:l:",
						long_options, &option_index)) >= 0) {
			switch (opt) {
			case 'p':
				p = strtoul(optarg, &ep, 10);
				if(!*optarg || *ep || port>65535 || port==0 ) {
					fprintf(stderr, "'%s' is not a valid port. Port numbers need to be >0 and <65536.\n", optarg);
					exit(1);
				}
				port=p;
				break;
			case 'l':
				d = strtod(optarg, &ep);
				if(!*optarg || *ep || d>100000 || d < 0.00099) {
					fprintf(stderr, "'%s' is not a valid timer value. The timer needs to be >0.001 and <100000.\n", optarg);
					exit(1);
				}
				loop_dly.tv_sec = (int)d;
				loop_dly.tv_usec = (int)((d-loop_dly.tv_sec)*1000000);
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

#ifndef DEMO
	KbusOpen();
#endif

	base = event_base_new();
	if (!base) {
		fprintf(stderr, "Could not initialize libevent: %m\n");
		return 1;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);

	listener = evconnlistener_new_bind(base, listener_cb, (void *)base,
	    LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
	    (struct sockaddr*)&sin,
	    sizeof(sin));

	if (!listener) {
		fprintf(stderr, "Could not create a listener: %m\n");
		return 1;
	}

	signal_event = evsignal_new(base, SIGINT, signal_cb, (void *)base);
	if (!signal_event || event_add(signal_event, NULL)<0) {
		fprintf(stderr, "Could not create/add a signal event: %m\n");
		return 1;
	}
	timer_event = event_new(base, -1, EV_TIMEOUT|EV_PERSIST, timer_cb, NULL);
	if (!timer_event || event_add(timer_event, &loop_dly)<0) {
		fprintf(stderr, "Could not create/add a timer event: %m\n");
		return 1;
	}

#ifndef DEMO
	KbusUpdate();
#endif
	event_base_dispatch(base);
#ifndef DEMO
	KbusClose();
#endif

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
	struct bufferevent *bev;

	bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	if (!bev) {
		fprintf(stderr, "Error constructing bufferevent: %m");
		event_base_loopbreak(base);
		return;
	}
	bufferevent_setcb(bev, NULL, NULL, conn_eventcb, NULL);
	bufferevent_enable(bev, EV_WRITE);
	bufferevent_disable(bev, EV_READ);

	bufferevent_write(bev, MESSAGE, strlen(MESSAGE));
}

static void
conn_eventcb(struct bufferevent *bev, short events, void *user_data)
{
	if (events & BEV_EVENT_EOF) {
		printf("Connection closed.\n");
	} else if (events & BEV_EVENT_ERROR) {
		printf("Got an error on the connection: %s\n",
		    strerror(errno));/*XXX win32*/
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
#ifdef DEMO
	printf("Loop.\n");
#else
	KbusUpdate();
#endif
}

