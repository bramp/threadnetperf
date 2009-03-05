#ifndef _THREADS_H
#define _THREADS_H

#include "common.h" // for struct settings

#include <pthread.h> // We assume we have a pthread library (even on windows)

#if defined(WIN32) || defined(__FreeBSD__)
	// Define some dummy structs, currently they do nothing
	typedef struct {
		unsigned long int __cpu_mask;
	} cpu_set_t;

	/* Access functions for CPU masks.  */
	#define CPU_ZERO(cpusetp) ((cpusetp)->__cpu_mask = 0)
	#define CPU_SET(cpu, cpusetp)
	#define CPU_CLR(cpu, cpusetp)
	#define CPU_ISSET(cpu, cpusetp)
#else
# include <sched.h>  // for cpu_set_t
#endif

void cpu_setup( cpu_set_t *cpu, unsigned int cores );

int thread_collect_results(const struct settings *settings, struct stats *total_stats, int (*print_results)(const struct settings *, const struct stats *, void * data), void *data);

int thread_join_all (int threaded_model);
int create_thread( void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset, unsigned int threaded_model );

// Allocate space for this many threads
int thread_alloc(size_t count);

int pthread_mutex_lock_block_signal (pthread_mutex_t *mutex, int signum);
int pthread_mutex_unlock_block_signal (pthread_mutex_t *mutex, int signum);

void threads_signal_all(int type, int threaded_model);
void threads_signal_parent(int threaded_model, __pid_t pid);
// Clear all allocated space for the threads
void threads_clear();
void wait_all(int threaded_model);

void wait_for_zero( pthread_mutex_t* mutex, pthread_cond_t* cond, volatile int * cond_variable );
void wait_for_nonzero( pthread_mutex_t* mutex, pthread_cond_t* cond, volatile int * cond_variable );

SOCKET create_stats_socket(); // TODO move this somewhere else

// Send the stats to an IPC
void send_stats_from_thread(struct stats stats);
#endif // _THREADS_H
