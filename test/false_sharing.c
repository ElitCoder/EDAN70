#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

#include <omp.h>
#include <pthread.h>

#define CACHE_BLOCK_SIZE	(64)
#define NUM_THREADS			(2)
#define WORK_ITERATIONS		(INT_MAX / 32)

typedef struct {
	int	id;
} work_arg_t;

static char*	a;

void* work_a(void* arg)
{
	work_arg_t*	p = (work_arg_t*)arg;
	int			i;
	int			id;
	int			tmp;
	
	id = p->id;
	
	for (i = 0; i < WORK_ITERATIONS; i++) {
		tmp = a[id];
		a[id] = tmp;
	}
	
	return NULL;
}

void* work_b(void* arg)
{
	work_arg_t*	p = (work_arg_t*)arg;
	int			i;
	int			id;
	int			tmp;
	
	id = p->id;
	
	for (i = 0; i < WORK_ITERATIONS; i++) {
		tmp = a[id * CACHE_BLOCK_SIZE];
		a[id * CACHE_BLOCK_SIZE] = tmp;
	}
	
	return NULL;
}

int main()
{
	pthread_t	threads[NUM_THREADS];
	double		start, end;
	
	a = calloc(CACHE_BLOCK_SIZE + 1, 1);
	
	start = omp_get_wtime();
	pthread_create(&threads[0], NULL, work_a, &(work_arg_t) { .id = 0 });
	pthread_create(&threads[1], NULL, work_a, &(work_arg_t) { .id = 1 });
	
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	end = omp_get_wtime();
	
	printf("time for misses: %1.2fs\n", end - start);
	
	start = omp_get_wtime();
	pthread_create(&threads[0], NULL, work_a, &(work_arg_t) { .id = 0 });
	pthread_create(&threads[1], NULL, work_b, &(work_arg_t) { .id = 1 });
	
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	end = omp_get_wtime();
	
	printf("time for no misses: %1.2fs\n", end - start);
	
	free(a);
	
	return 0;
}