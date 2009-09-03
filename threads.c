#include "threads.h"
#include "common.h"    // for struct settings
#include "serialise.h" // for send_results
#include "remote.h"
#include "global.h"    // for bRunning


#include <assert.h>
#include <string.h> // for malloc
#include <stdio.h>  // for printf
#include <stdlib.h> // for exit

#ifndef WIN32
#include <unistd.h>   //for fork
#include <sys/wait.h> //for waitpid
#endif

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

char* ipc_sock_name;

#ifdef WIN32
// Sleep for a number of microseconds
int usleep(unsigned int microseconds) {
	struct timespec waittime;

	waittime.tv_sec = microseconds / 1000000;
	waittime.tv_nsec = (microseconds % 1000000) * 1000;

	pthread_delay_np ( &waittime );
	return 0;
}
#endif

#if defined(WIN32) || defined(__FreeBSD__)
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
#if defined(__FreeBSD__)
//		cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, *pid, ..//TODO finish
#else
		sched_setaffinity(*pid, cpusetsize, cpuset);		
#endif
		//Call start_routine
		(*start_routine)(arg);
		_exit(0);
	}
	else if (*pid == -1 ) {
		fprintf(stderr, "%s:%d process_create_on() error (%d) %s\n", __FILE__, __LINE__, errno, strerror(errno));
		return -1;
	}

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
int create_thread( void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset, unsigned int threaded_model ) {
	int ret = -1;

	assert (start_routine != NULL);

	if ( thread_count >= thread_max_count ) {
		fprintf(stderr, "%s:%d create_thread() can only create %ud threads\n", __FILE__, __LINE__, (unsigned int) thread_max_count );
		return -1;
	}

	switch ( threaded_model ) {
		case MODEL_THREADED :
			ret = pthread_create_on( &thread[thread_count].tid, NULL, start_routine, arg , cpusetsize, cpuset);
			break;
		case MODEL_PROCESS :
			ret = process_create_on( &thread[thread_count].pid, start_routine, arg, cpusetsize, cpuset);
			break;
		default:
			assert( 0 );
	}

	if ( !ret )
		thread_count++;

	return ret;
}

void send_stats_from_thread(struct stats stats) {
	unsigned int sock_len;
	struct sockaddr_un	ipc_socket;
	int s;

	// Create the socket to send the details back on
	s = socket( AF_UNIX, SOCK_DGRAM, 0);

	if ( s == INVALID_SOCKET ) {
		fprintf(stderr, "%s:%d socket() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		return;
	}

	if ( set_socket_timeout(s, IPC_TIMEOUT) ) {
		fprintf(stderr, "%s:%d set_socket_timeout() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}


	ipc_socket.sun_family = AF_UNIX;

	//strcpy(ipc_socket.sun_path, IPC_SOCK_NAME);
	sprintf(ipc_socket.sun_path , "%s", ipc_sock_name);
	sock_len = strlen(ipc_socket.sun_path) + sizeof(ipc_socket.sun_family);

	// Bind the IPC SOCKET
	if ( connect( s,(struct sockaddr *) &ipc_socket, sock_len) == SOCKET_ERROR) {
		fprintf(stderr, "%s:%d connect() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO));
		goto cleanup;
	}

	if ( send_results(s, &stats) ) {
		fprintf(stderr, "%s:%d send_results() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

cleanup:
	closesocket(s);
}

SOCKET create_stats_socket() {
	struct sockaddr_un	ipc_socket;
	int sock_len;
	// Create the socket to receive the stats data
	SOCKET s = socket( AF_UNIX, SOCK_DGRAM, 0);

	if ( s == INVALID_SOCKET ) {
		fprintf(stderr, "%s:%d socket() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		return SOCKET_ERROR;
	}

	if ( set_socket_timeout(s, IPC_TIMEOUT) ) {
		fprintf(stderr, "%s:%d set_socket_timeout() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	ipc_socket.sun_family = AF_UNIX;
	ipc_sock_name = tempnam(NULL, "threadnetperf");
	sprintf(ipc_socket.sun_path, "%s", ipc_sock_name);

	sock_len = strlen(ipc_socket.sun_path) + sizeof(ipc_socket.sun_family);
	unlink(ipc_socket.sun_path);

	// Bind the IPC SOCKET
	if ( bind( s, &ipc_socket, sock_len) == SOCKET_ERROR) {
		fprintf(stderr, "%s:%d bind() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	return s;

cleanup:
	unlink(ipc_sock_name);
	closesocket(s);
	return SOCKET_ERROR;
}

// Join all these threads

int thread_join_all(int threaded_model) {
	while (thread_count > 0) {
		thread_count--;
		if( threaded_model == MODEL_THREADED ) {
			pthread_join( thread[thread_count].tid, NULL );
		} else {
			int status;
			waitpid(thread[thread_count].pid, &status, 0);
//			if(WIFEXITED(status))
//				fprintf(stderr, "%s:%d waitpid() client (%d) exited with stats (%d) \n", __FILE__, __LINE__, thread[thread_count].pid, status );		
		}
	}
	assert ( thread_count == 0 );
	return 0;
}

int thread_collect_results(const struct settings *settings, struct stats *total_stats, int (*print_results)(const struct settings *, const struct stats *, void *), void *data) {
	unsigned int i = 0;
	SOCKET s;

	assert( settings != NULL );
	assert( total_stats != NULL );

	s = ((struct remote_data*)data)->stats_socket;
	assert ( s != INVALID_SOCKET );

	if ( settings->verbose )
		printf("Collecting %u results\n",  settings->servercores);

	for ( ; i < settings->servercores; i++ ) {
		struct stats stats;

		if ( read_results(s, &stats) != 0 ) {
			fprintf(stderr, "%s:%d read_results() error\n", __FILE__, __LINE__ );
			return -1;
		}

		if ( print_results(settings, &stats, data) ) {
			fprintf(stderr, "%s:%d print_results() error\n", __FILE__, __LINE__ );
			return -1;
		}

		// Now add the values to the total
		stats_add( total_stats, &stats );
	}

	// Divide the duration by the # of CPUs used
	if ( settings->servercores > 1 )
		total_stats->duration /= settings->servercores;

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

void threads_signal(pid_t pid, int type) {
	union sigval v;

	memset(&v, 0, sizeof(v));
	v.sival_int = type;
// TODO change to fprint
	if ( sigqueue(pid, SIGNUM, v) )
		printf("(%d) Error %d %s sending signal %d\n", getpid(), ERRNO, strerror(errno), type);	
}

void threads_signal_parent(int type, int threaded_model) {	
	if ( threaded_model == MODEL_PROCESS )
		threads_signal(getppid(), type);
	else if ( threaded_model == MODEL_THREADED )
		threads_signal(getpid(), type);
	else
		assert ( 0 );
}

void threads_signal_all(int type, int threaded_model) {	
	if(	threaded_model == MODEL_PROCESS ) {
		int i;
		for(i=0; i<thread_count; i++) {
			threads_signal(thread[i].pid, type);
		}
	} else if ( threaded_model == MODEL_THREADED ) {
		threads_signal(getpid(), type);
	} else
		assert ( 0 );
}

/**
 * Obtains a mutex whilst blocking signal handle
 * Return 0 on success, otherwise a error number
 * The signal is not blocked if a error occurs
 */
int pthread_mutex_lock_block_signal (pthread_mutex_t *mutex, int signum) {
	int ret;
	sigset_t set;
	sigemptyset(&set);

	if ( sigaddset(&set, signum) ) {
		fprintf(stderr, "%s:%d sigaddset() error\n", __FILE__, __LINE__ );
		return -1;
	}
	if ( pthread_sigmask( SIG_BLOCK, &set, NULL) ) {
		fprintf(stderr, "%s:%d pthread_sigmask() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		return -1;
	}
	
	ret = pthread_mutex_lock(mutex);
	if ( ret ) {
		if ( pthread_sigmask( SIG_UNBLOCK, &set, NULL) ) {
			fprintf(stderr, "%s:%d pthread_sigmask() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			return -1;
		}
	}
	return ret;
}

/**
 * Unlocks a mutex whilst unblocking a signal
 * Return 0 on success, otherwise a error number
 * The signal is still blocked if a error occurs
 */
int pthread_mutex_unlock_block_signal (pthread_mutex_t *mutex, int signum) {
	int ret;
	sigset_t set;
	sigemptyset(&set);

	if ( sigaddset(&set, signum) ) {
		fprintf(stderr, "%s:%d sigaddset() error\n", __FILE__, __LINE__ );
		return -1;
	}

	ret = pthread_mutex_unlock(mutex);
	if ( !ret ) {
		if ( pthread_sigmask( SIG_UNBLOCK, &set, NULL) ) {
			fprintf(stderr, "%s:%d pthread_sigmask() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			return -1;
		}		
	}
	
	return ret;
}

// Wait for a condition to be true
void wait_for_zero( pthread_mutex_t* mutex, pthread_cond_t* cond, volatile int* cond_variable ) {
	pthread_mutex_lock_block_signal( mutex, SIGNUM );
	while ( bRunning && *cond_variable > 0 ) {
		struct timespec abstime;

		get_timespec_now(&abstime);
		add_timespec    (&abstime, 0, 10000000); // add 10ms

		pthread_cond_timedwait( cond, mutex, &abstime);

		pthread_mutex_unlock_block_signal( mutex, SIGNUM );
		// HACK The signal handler is now reenabled, so any queued signals can go
		pthread_mutex_lock_block_signal( mutex, SIGNUM );		
	}
	pthread_mutex_unlock_block_signal( mutex, SIGNUM );
}

void wait_for_nonzero( pthread_mutex_t* mutex, pthread_cond_t* cond, volatile int* cond_variable ) {
	pthread_mutex_lock_block_signal( mutex, SIGNUM );
	while ( bRunning && *cond_variable == 0 ) {
		struct timespec abstime;

		get_timespec_now(&abstime);
		add_timespec    (&abstime, 0, 10000000); // add 10ms

		pthread_cond_timedwait( cond, mutex, &abstime);

		pthread_mutex_unlock_block_signal( mutex, SIGNUM );
		// HACK The signal handler is now reenabled, so any queued signals can go
		pthread_mutex_lock_block_signal( mutex, SIGNUM );		
	}
	pthread_mutex_unlock_block_signal( mutex, SIGNUM );
}

