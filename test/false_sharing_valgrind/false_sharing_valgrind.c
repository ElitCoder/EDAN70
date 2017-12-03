#if 0
#include <pthread.h>

pthread_mutex_t	g_mutex;
static int		g_value[2];

void* a(void* args)
{
	int	loc = g_value[0];
	
	sleep(4);
	
	loc = g_value[1];
	
	return NULL;
}

void* b(void* args)
{
	sleep(1);
	
	g_value[1] = 1;
	
	return NULL;
}

int main(void)
{
	pthread_t	threads[2];
	
	pthread_create(&threads[0], NULL, a, NULL);
	pthread_create(&threads[1], NULL, b, NULL);
	
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	
	return 0;
}
#endif

#if 0
#include <pthread.h>

static int	x[2];
static int	y[1000];
static int	z[1000];

void* a(void* args)
{
	for (int i = 0; i < 1000; i++)
		x[0] += y[i] * z[i];
}

void* b(void* args)
{
	for (int i = 0; i < 1000; i++)
		x[1] += y[i] * z[i];
}

int main(void)
{
	pthread_t	threads[2];
	
	pthread_create(&threads[0], NULL, a, NULL);
	pthread_create(&threads[1], NULL, b, NULL);
	
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	
	printf("%d\n", x[1]);
	printf("%d\n", x[0]);
	
	return 0;
}
#endif

#if 0
#include <omp.h>

int main(void)
{
	int*	result = malloc(sizeof(int) * 2);
	int		i;
	
	omp_set_num_threads(2);
	
	#pragma omp parallel for
	for (i = 0; i < 100000; i++)
		result[omp_get_thread_num()]++;
		
	return 0;
}
#endif

#if 0
#include <pthread.h>

static char*	g_p;

void* a(void* args)
{
	g_p[0] = 0;
	
	return NULL;
}

void* b(void* args)
{
	g_p[1] = 0;
	
	return NULL;
}

int main(void)
{
	pthread_t	threads[2];
	
	g_p = malloc(128);
	
	pthread_create(&threads[0], NULL, a, NULL);
	pthread_create(&threads[1], NULL, b, NULL);
	
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	
	return 0;
}
#endif

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