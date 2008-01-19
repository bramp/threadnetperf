#include "threads.h"
#include "common.h" // for struct settings
#include "print.h" // for print_results

#include <assert.h>
#include <malloc.h>

 // Array to handle thread handles
pthread_t *thread = NULL;

 // Total number of threads
size_t thread_count = 0;

 // Total number of threads
size_t thread_max_count = 0;

#ifdef WIN32
// Sleep for a number of microseconds
int usleep(unsigned int useconds) {
	struct timespec waittime;

	if ( useconds > 1000000 )
		return EINVAL;

	waittime.tv_sec = 0;
	waittime.tv_nsec = useconds * 1000; 

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

int thread_sum_stats(const struct settings *settings, struct stats *total_stats) {
	unsigned int i = 0;

	while (thread_count > 0) {
		struct stats *stats;

		thread_count--;
		pthread_join( thread[thread_count], (void **)&stats );

		if ( stats != NULL ) {
			print_results( settings, stats );

			// Now add the values to the total
			total_stats->bytes_received += stats->bytes_received;
			total_stats->duration       += stats->duration;
			total_stats->pkts_received  += stats->pkts_received;
			total_stats->pkts_time      += stats->pkts_time;

			i++;
		}
	}

	// Divide the duration by the # of CPUs used
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

	thread_count = count;
	return 0;
}

void threads_clear() {
	free( thread );
	thread = NULL;
	thread_count = 0;
}
