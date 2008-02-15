#include "threads.h"
#include "common.h" // for struct settings
#include "serialise.h" // for send_results

#include <assert.h>
#include <malloc.h>
#include <string.h> // for malloc
#include <stdio.h> // for printf

 // Array to handle thread handles
pthread_t *thread = NULL;

 // Total number of threads
size_t thread_count = 0;

 // Total number of threads
size_t thread_max_count = 0;

#ifdef WIN32
// Sleep for a number of microseconds
int usleep(unsigned int microseconds) {
	struct timespec waittime;

	waittime.tv_sec = microseconds / 1000000;
	waittime.tv_nsec = (microseconds % 1000000) * 1000; 

	pthread_delay_np ( &waittime );
	return 0;
}

int pthread_attr_setaffinity_np ( pthread_attr_t *attr, size_t cpusetsize, const cpu_set_t *cpuset) {
	// TODO Make this set affidenitys on windows
	return 0;
}
#endif

/**
	Create a thread on a specific core(s)
*/
int pthread_create_on( pthread_t *t, pthread_attr_t *attr, void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset) {

	pthread_attr_t thread_attr;
	int ret;

	if (attr == NULL) {
		pthread_attr_init ( &thread_attr );
		attr = &thread_attr;
	}

	// Set the CPU
	ret = pthread_attr_setaffinity_np( attr, cpusetsize, cpuset );
	if (ret)
		goto cleanup;

	// Make sure the thread is joinable
	ret = pthread_attr_setdetachstate( attr, PTHREAD_CREATE_JOINABLE);
	if (ret)
		goto cleanup;

	// Now create the thread
	ret = pthread_create(t, attr, start_routine, arg);

cleanup:
	if ( attr == &thread_attr )
		pthread_attr_destroy ( &thread_attr );

	return ret;
}


// Creates a new thread and adds it to the thread array
int create_thread( void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset ) {
	int ret; 
	
	if ( thread_count >= thread_max_count )
		return -1;
	
	ret = pthread_create_on( &thread[thread_count], NULL, start_routine, arg , cpusetsize, cpuset);

	if ( !ret )
        thread_count++;

	return ret;
}

// Join all these threads
int thread_join_all() {
	while (thread_count > 0) {
		thread_count--;
		pthread_join( thread[thread_count], NULL );
	}
	assert ( thread_count == 0 );
	return 0;
}

int thread_collect_results(const struct settings *settings, struct stats *total_stats, int (*print_results)(const struct settings *, const struct stats *, void * data), void *data) {
	unsigned int i = 0;

	assert( settings != NULL );
	assert( total_stats != NULL );

	while (thread_count > 0) {
		struct stats *stats = NULL;
		void * stats_void = NULL;

		thread_count--;
		pthread_join( thread[thread_count], &stats_void );

		stats = (struct stats *) stats_void;

		if ( stats != NULL ) {
			if ( print_results(settings, stats, data) ) {
				fprintf(stderr, "%s:%d print_results() error\n", __FILE__, __LINE__ );
				return -1;
			}

			// Now add the values to the total
			stats_add( total_stats, stats );

			i++;
		}
	}

	// Divide the duration by the # of CPUs used
	if ( i > 1 )
		total_stats->duration /= i;

	assert ( thread_count == 0 );

	return 0;
}

int thread_alloc(size_t count) {
	assert ( thread == NULL );
	assert ( thread_count == 0 );

	thread = calloc( count, sizeof(*thread) );
	if ( !thread )
		return -1;

	thread_max_count = count;
	thread_count = 0;

	return 0;
}

void threads_clear() {
	free( thread );
	thread = NULL;
	thread_count = 0;
	thread_max_count = 0;
}
