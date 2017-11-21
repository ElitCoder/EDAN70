#include <stdio.h>
#include <limits.h>

#include <pthread.h>

#include "timer.h"

#define CACHE_LINE_SIZE	(64)
//#define LOOP_LENGTH		(INT_MAX / 10)
#define LOOP_LENGTH		(100)

/*	Normal structure, a and b will probably exist in the same 
	cache block */
typedef struct {
	int	a;
	int	b;
} bad_t;

/*	By inserting padding to make a and b reside in different
	cache blocks we avoid false sharing */
typedef struct {
	int		a;
	char	pad[CACHE_LINE_SIZE];
	int		b;
} good_t;

static bad_t	b;
static good_t	g;

void* sum_a(void* args)
{
	int	s;
	int	i;
	
	if (args) { }
	
	for (i = 0; i < LOOP_LENGTH; i++)
		s += b.a;
		
	return NULL;
}

void* inc_b(void* args)
{
	int	i;
	
	if (args) { }

	for (i = 0; i < LOOP_LENGTH; i++)
		b.b++;
		
	return NULL;
}

void* sum_a_good(void* args)
{
	int	s;
	int	i;

	if (args) { }

	for (i = 0; i < LOOP_LENGTH; i++)
		s += g.a;
		
	return NULL;
}

void* inc_b_good(void* args)
{
	int	i;
	
	if (args) { }

	for (i = 0; i < LOOP_LENGTH; i++)
		g.b++;
		
	return NULL;
}

int main(void)
{
	pthread_t	threads[2];
	
	start_timer();
	pthread_create(&threads[0], NULL, sum_a, NULL);
	pthread_create(&threads[1], NULL, inc_b, NULL);
	
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	printf("b: %1.2fs\n", stop_timer());
	
	start_timer();
	pthread_create(&threads[0], NULL, sum_a_good, NULL);
	pthread_create(&threads[1], NULL, inc_b_good, NULL);
	
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	printf("g: %1.2fs\n", stop_timer());
	
	return 0;
}