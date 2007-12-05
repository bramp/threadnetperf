/***
	A network benchmark tool
	by Andrew Brampton (2007)

	Note, this app is very rough, and needs cleaning up, but it works!
*/

#include "common.h"

// Condition Variable that is signed each time a thread is ready
pthread_cond_t ready_cond;

// Condition Variable to indicate when all the threads are connected and ready to go
pthread_cond_t go_cond;
pthread_mutex_t go_mutex;

// Flag to indidcate if we are still running
volatile int bRunning = 1;

// Count of how many threads are ready
volatile int unready_threads = 0;

// The message size
int message_size = 65535;

// How long (in seconds) this should run for
int duration = 10;

// Should we diable Nagle's Algorithm
int disable_nagles = 1;



#ifndef pthread_attr_setaffinity_np
	int pthread_attr_setaffinity_np ( pthread_attr_t *attr, size_t cpusetsize, const cpu_set_t *cpuset) {
		return 0;
	}
#endif

/**
	Create a thread on a specific core(s)
*/
int pthread_create_on( pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset) {

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

	ret = pthread_create(thread, attr, start_routine, arg);

cleanup:
	if ( attr == &thread_attr )
		pthread_attr_destroy ( &thread_attr );

	return ret;
}

#ifdef WIN32
/**
	Function to setup the winsock libs
*/
void setup_winsock() {
	WSADATA wsaData;

	if ( WSAStartup(MAKEWORD(2,2), &wsaData) ) {
		fprintf(stderr, "%s: %d WSAStartup() error\n", __FILE__, __LINE__ );
		return;
	}
}

void cleanup_winsock() {
	WSACleanup();
}
#endif

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
#endif

/**
	Wait until duration has passed
*/
void pause_for_duration(unsigned int duration) {
	long long start_time; // The time we started

	// Make sure duration is in microseconds
	duration = duration * 1000000;

	// This main thread controls when the test ends
	start_time = get_microseconds();

	while ( bRunning ) {
		long long now = get_microseconds();

		if ( now - start_time > duration ) {
			bRunning = 0;
			break;
		}

#ifdef _DEBUG
		printf(".");
		fflush(stdout);
#endif

		usleep( 100000 );
	}
}

int parse_arguments( int argc, char *argv[] ) {
	int c;

	// Default arguments
	message_size = 1024;
	disable_nagles = 0;
	duration = 10;

	// Lets parse some command line args
	while ((c = getopt(argc, argv, "tuns:d:")) != -1) {
		switch ( c ) {
			// Duration
			case 'd':
				duration = atoi( optarg );
				if ( duration == 0 ) {
					fprintf(stderr, "Invalid duration given (%s)\n", optarg );
					return -1;
				}
				break;

			// Disable nagles algorithm (ie NO delay)
			case 'n':
				disable_nagles = 1;
				break;

			// Parse the message size
			case 's':
				message_size = atoi( optarg );
				if ( message_size == 0 ) {
					fprintf(stderr, "Invalid message size given (%s)\n", optarg );
					return -1;
				}
				break;
			
			case '?':
				fprintf(stderr, "Unknown argument (%s)\n", argv[optind-1] );
				return -1;

			// TCP/UDP
			case 't':
			case 'u':
			default:
				fprintf(stderr, "Argument not implemented (yet) (%c)\n", c );
				return -1;
		}
	}

	{
		// Make a 2D array for each possible to and from
		int *clientserver = malloc ( 8 * 8 * sizeof(*clientserver) );

		// Try and parse anything else left on the end
		// 1{0-0} 10{1-1} 3{0-1}, 1 connection core 0 to core 0, 10 connections core 1 to core 1, and 3 connections core 0 to core 1
		if (optind < argc) {
			char *c = argv[optind];
			char *c2;

			int count; // Number of connections in this class
			int client, server; // Client and Server cores



			if ( count == 0 ) {
				fprintf(stderr, "Invalid count (%s)\n", argv[optind] );
				return -1;
			}
			
			c2 = strchr( c2, '-' );
			if ( c2 == NULL ) {
				fprintf(stderr, "Unknown argument (%s)\n", argv[optind] );
				return -1;
			}
			*c2 = '\0';

			//clientserver [ client ] [ server ] += count;

			optind++;
		}

	}

	return 0;
}

int main (int argc, char *argv[]) {
	struct server_request sreq;
	struct client_request creq;
	pthread_t *thread; // Array to handle thread handles
	unsigned int threads; // Total number of threads
	unsigned int i;
	cpu_set_t cpus;
	int ret;

#ifdef WIN32
	setup_winsock();
#endif

	if ( parse_arguments( argc, argv ) ) {
		goto cleanup;
	}

	threads = 2;
	unready_threads = threads;
	thread = malloc( threads * sizeof(*thread) );
	memset (thread, 0, threads * sizeof(*thread));

	// Setup the shared conditional variables
	ret = pthread_cond_init( &go_cond, NULL);
	if ( ret ) {
		fprintf(stderr, "%s: %d pthread_cond_init() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}
	ret = pthread_mutex_init( &go_mutex, NULL);
	if ( ret ) {
		fprintf(stderr, "%s: %d pthread_mutex_init() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Create all the threads
	CPU_ZERO ( &cpus );
	CPU_SET ( 0, &cpus );

	// Create all the server threads
	sreq.port = 1234;
	ret = pthread_create_on( &thread[0], NULL, server_thread, &sreq, sizeof(cpus), &cpus);
	if ( ret ) {
		fprintf(stderr, "%s: %d pthread_create_on() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Create all the client threads
	creq.addr.sin_family = AF_INET;
	creq.addr.sin_addr.s_addr = inet_addr( "127.0.0.1" );
	creq.addr.sin_port = htons( 1234 );
	creq.n = 1;

	ret = pthread_create_on( &thread[1], NULL, client_thread, &creq, sizeof(cpus), &cpus);
	if ( ret ) {
		fprintf(stderr, "%s: %d pthread_create_on() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Spin lock until all the threads are ready
	// TODO change this to use a semaphore
	pthread_mutex_lock( &go_mutex );
	while ( unready_threads > 0 ) {
		pthread_mutex_unlock( &go_mutex );
		usleep( 1000 );
		pthread_mutex_lock( &go_mutex );
	}
	pthread_cond_broadcast( &go_cond );
	pthread_mutex_unlock( &go_mutex );

	// Now wait until the test is completed
	pause_for_duration( duration );

#ifdef _DEBUG
	printf("\nFinished\n" );
#endif

cleanup:

	bRunning = 0;

	// Block waiting until all threads die
	for (i = 0; i < threads; i++) {
		assert ( thread [i] != 0 );
		pthread_join( thread[i], NULL );
	}

	pthread_cond_destroy( & go_cond );
	pthread_mutex_destroy( & go_mutex );

	free ( thread );

#ifdef WIN32
	cleanup_winsock();
#endif

	return 0;
}
