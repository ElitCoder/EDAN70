#ifndef TIMER_H
#define TIMER_H

#include <sys/time.h>

struct timer_t {
	struct timeval	s;
	double			l;
};

void 	start_timer(void);
double	stop_timer(void);

#endif