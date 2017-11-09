#include <stdlib.h>

#include "timer.h"

static struct timer_t	timer;

void start_timer(void)
{
	gettimeofday(&timer.s, NULL);
}

double stop_timer(void)
{
	struct timeval	stop;
	double			time;
	
	gettimeofday(&stop, NULL);
	
	time = (stop.tv_sec - timer.s.tv_sec) * 1000.0;
	time += (stop.tv_usec - timer.s.tv_usec) / 1000.0;
	
	return time / 1000.0;
}