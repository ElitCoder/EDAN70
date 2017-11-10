#include <stdio.h>
#include <pthread.h>

int	z;
int y;

static int x;

void* a(void* args)
{
	if (x == 0)
		z = 1;

	return NULL;
}

void* b(void* args)
{
	if (x == 0)
		y = 1;

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
