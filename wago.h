#ifndef WAGO_H
#define WAGO_H

struct event_base;
extern struct event_base *base;

extern char debug;

#ifdef DEMO
extern char demo_state_r;
extern char demo_state_w;
#endif

#endif
