#ifndef _THREADS_H
#define _THREADS_H

#include "common.h" // for struct settings

#include <pthread.h> // We assume we have a pthread library (even on windows)

#ifdef WIN32
	// Define some dummy structs, currently they do nothing
	typedef struct {
		unsigned long int __cpu_mask;
	} cpu_set_t;

	/* Access functions for CPU masks.  */
	#define CPU_ZERO(cpusetp) ((cpusetp)->__cpu_mask = 0)
	#define CPU_SET(cpu, cpusetp)
	#define CPU_CLR(cpu, cpusetp)
	#define CPU_ISSET(cpu, cpusetp)
#endif

void cpu_setup( cpu_set_t *cpu, unsigned int cores );

int thread_collect_results(const struct settings *settings, struct stats *total_stats, int (*print_results)(const struct settings *, const struct stats *, void * data), void *data);

int thread_join_all ();
int create_thread( void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset );

// Allocate space for this many threads
int thread_alloc(size_t count);

// Clear all allocated space for the threads
void threads_clear();

#endif // _THREADS_H
