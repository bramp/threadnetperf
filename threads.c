#include "threads.h"
#include "common.h"    // for struct settings
#include "serialise.h" // for send_results

#include <assert.h>
#include <malloc.h>
#include <string.h> // for malloc
#include <stdio.h>  // for printf
#include <stdlib.h> // for exit

#include <unistd.h>   //for fork
#include <sys/wait.h> //for waitpid

 // Array to handle thread handles
union thread_ids
{
   pthread_t 	tid;
   pid_t		pid;
};

union thread_ids *thread;

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

void cpu_setup( cpu_set_t *cpu, unsigned int cores ) {
	unsigned int core = 0;
	
	CPU_ZERO ( cpu );

	// Set all the correct bits
	while ( cores > 0 ) {
		if ( cores & 1 )
			CPU_SET ( core , cpu );
		cores = cores >> 1;
		core++;
	}
}

/* 
 * Create a process on a specific core
 */ 
int process_create_on(pid_t *pid,  void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset) {
	
	*pid = fork();
	if( *pid == 0) {
		*pid = getpid();
		sched_setaffinity(*pid, cpusetsize, cpuset);
		printf("Created child process %d (parents pid is %d)\n", *pid, getppid());
		//Call start_routing
		(*start_routine)(arg);
		exit(0);
	}
	else if (*pid == -1 )
		return -1;

/*
		int rv;
		printf("Parent process is %d With child %d \n", getpid(), *pid);
		wait(&rv);
		printf("Child exited with %d", WEXITSTATUS(rv));
	}
*/

	return 0;
}

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
	if ( cpuset != NULL ) {
		ret = pthread_attr_setaffinity_np( attr, cpusetsize, cpuset );
		if (ret)
			goto cleanup;
	}

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
int create_thread( void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset, int threaded_model ) {
	int ret;

	assert (start_routine != NULL);

	if ( thread_count >= thread_max_count )
		return -1;

	// TODO give threaded_model hash define names, and then use a switch statement
	if ( threaded_model )
		ret = pthread_create_on( &thread[thread_count].tid, NULL, start_routine, arg , cpusetsize, cpuset);
	else if ( threaded_model == 0 )
		ret = process_create_on( &thread[thread_count].pid, start_routine, arg, cpusetsize, cpuset);
	else
		assert ( 0 );

	if ( !ret )
        thread_count++;

	return ret;
}

// Join all these threads
int thread_join_all(int threaded_model) {
	while (thread_count > 0) {
		thread_count--;
		if(threaded_model) {
			
			pthread_join( thread[thread_count].tid, NULL );
		} else {
			int status;
			waitpid(thread[thread_count].pid, &status, 0);
			if(WIFEXITED(status))
				fprintf(stderr, "%s:%d waitpid() client (%d) exited with stats (%d) \n", __FILE__, __LINE__, thread[thread_count].pid, status );		
		}
	} 
	assert ( thread_count == 0 );
	return 0;
}

int thread_collect_results(const struct settings *settings, struct stats *total_stats, int (*print_results)(const struct settings *, const struct stats *, void *), void *data) {
	unsigned int i = 0;

	assert( settings != NULL );
	assert( total_stats != NULL );

	while (thread_count > 0) {
		struct stats *stats = NULL;
		void * stats_void = NULL;

		thread_count--;
		if( settings->threaded_model ) {
			pthread_join( thread[thread_count].tid, &stats_void );
		} else {
			//TODO: Add a pipe to the send the states across.
			int status;
			waitpid( thread[thread_count].pid , &status, 0);
		}

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

	thread = calloc(count, sizeof(*thread));
	
	if ( !thread )
		return -1;

	thread_max_count = count;
	thread_count = 0;

	return 0;
}

void threads_clear() {
	free ( thread );

	thread = NULL;
	thread_count = 0;
	thread_max_count = 0;
}
