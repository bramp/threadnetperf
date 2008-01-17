/***
	A multi-threaded network benchmark tool
	by Andrew Brampton (2007)

	Note, this app is very rough, and needs cleaning up, but it works!
	TODO Allow the app to work across networks
	TODO Implement optomisations
	TODO Add flag to output bandwidth at set intervals during the experiment
	TODO Add flag to allow iteration until a confidence interval is meant
*/

#include "common.h"
#include "print.h"
#include "server.h"

#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>

#ifdef WIN32
	#include "getopt.h"

	/* Access functions for CPU masks.  */
	#define CPU_ZERO(cpusetp)
	#define CPU_SET(cpu, cpusetp)
	#define CPU_CLR(cpu, cpusetp)
	#define CPU_ISSET(cpu, cpusetp)

#else
	#include <unistd.h>
#endif

// Condition Variable that is signaled each time a thread is ready
pthread_cond_t ready_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t ready_mutex = PTHREAD_MUTEX_INITIALIZER;

// Condition Variable to indicate when all the threads are connected and ready to go
pthread_cond_t go_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t go_mutex = PTHREAD_MUTEX_INITIALIZER;


// Flag to indidcate if we are still running
int bRunning = 1;

// Count of how many threads are ready
unsigned int unready_threads = 0;

// The number of cores this machine has
const unsigned int cores = 8; // TODO get the read number!

// Array of all the server requests
struct server_request *sreq = NULL;

// Array of all the client requests
struct client_request *creq = NULL;

// Settings

// Make a 2D array for each possible to and from connection
int **clientserver = NULL;

// Signals all threads to stop
void stop_all () {
	unsigned int i;

	bRunning = 0;

	if ( creq ) { 
		for (i = 0; i < cores; i++) {
			creq[i].bRunning = 0;
		}
	}

	if ( sreq ) {
		for (i = 0; i < cores; i++) {
			sreq[i].bRunning = 0;
		}
	}
}

/**
	Wait until duration has passed
*/
void pause_for_duration(const struct settings *settings) {
	long long end_time; // The time we need to end

	// Make sure duration is in microseconds
	long long duration;

	assert ( settings != NULL );

	// This main thread controls when the test ends
	duration = settings->duration * 1000000;
	end_time = get_microseconds() + duration;

	while ( bRunning ) {
		long long now = get_microseconds();

		if ( now > end_time ) {
			stop_all();
			break;
		}

		if ( settings->verbose ) {
			pthread_mutex_lock( &printf_mutex );
			printf(".");
			fflush(stdout);
			pthread_mutex_unlock( &printf_mutex );
		}

		// Pause for 0.1 second
		usleep( 100000 );
	}
	
	printf("\n");
}

void print_usage() {

	fprintf(stderr, "threadnetperf by bramp 2007\n" );
	fprintf(stderr, "Usage: threadnetperf [options] tests\n" );
	fprintf(stderr, "Usage: threadnetperf -D [options]\n" );
	fprintf(stderr, "Runs a threaded network test\n" );

	fprintf(stderr, "\n" );

	fprintf(stderr, "	-c level   Confidence level, must be 95 or 99\n");
	fprintf(stderr, "	-D         Use deamon mode (wait for incoming tests)\n" );
	fprintf(stderr, "	-d time    Set duration to run the test for\n" );
	fprintf(stderr, "	-e         Eat the data (i.e. dirty it)\n");
	fprintf(stderr, "	-H host    Set the remote host(and port) to connect to\n");
	fprintf(stderr, "	-h         Display this help\n");
	fprintf(stderr, "	-i min,max Set the minimum and maximum iterations\n");	
	fprintf(stderr, "	-n         Disable Nagle's algorithm (e.g no delay)\n" );
	fprintf(stderr, "	-p port    Set the port number for the first server thread to use\n" );
	fprintf(stderr, "	-s size    Set the send/recv size\n" );
	fprintf(stderr, "	-T         Timestamp packets, and measure latency\n" );
	fprintf(stderr, "	-t         Use TCP\n" );
	fprintf(stderr, "	-u         Use UDP\n" );
	fprintf(stderr, "	-v         Verbose\n" );

	fprintf(stderr, "\n" );
	fprintf(stderr, "	tests      Combination of cores and clients\n" );
	fprintf(stderr, "		N{c-s}   N connections\n" );
	fprintf(stderr, "		         c client core\n" );
	fprintf(stderr, "		         s server core\n" );

	fprintf(stderr, "\n" );
	fprintf(stderr, "Examples:\n" );
	fprintf(stderr, "	> threadnetperf -n -s 10000 1{0-0}\n" );
	fprintf(stderr, "	Disable Nagle's, send size of 10000 with 1 connection from core 0 to core 0\n" );
	
	fprintf(stderr, "\n" );
	fprintf(stderr, "	> threadnetperf 10{0-0} 10{1-1} 10{2-2}\n" );
	fprintf(stderr, "	10 connection from core 0 to core 0, 10 connections from core 1 to core 1, and 10 connections from core 2 to core 2\n" );

	//fprintf(stderr, "-d\n" );
	//fprintf(stderr, "-d\n" );
}

int parse_arguments( int argc, char *argv[], struct settings *settings ) {
	int c;
	unsigned int x, y;
	unsigned int tests = 0;

	assert ( settings != NULL );

	// Default arguments
	settings->deamon = 0;
	settings->message_size = 1024;
	settings->socket_size = -1;
	settings->disable_nagles = 0;
	settings->duration = 10;
	settings->server_host = "127.0.0.1";
	settings->port = 1234;
	settings->verbose = 0;
	settings->dirty = 0;
	settings->timestamp = 0;
	settings->confidence_lvl = 0;
	settings->min_iterations = 1;
	settings->max_iterations = 1;

	settings->type = SOCK_STREAM;
	settings->protocol = IPPROTO_TCP;

	if ( argc == 1 ) {
		print_usage();
		return -1;
	}

	// Lets parse some command line args
	while ((c = getopt(argc, argv, "DtTeunvhs:d:p:c:i:H:")) != -1) {
		switch ( c ) {

			case 'c': //confidence level, must be either 95 or 99
				settings->confidence_lvl = atoi(optarg);
				
				if(settings->confidence_lvl != 95 || settings->confidence_lvl != 99) {
					fprintf(stderr, "Confidence Level must be 95 or 99. Given (%s)\n", optarg);
					return -1;
				}

				break;

			// Deamon mode (wait for incoming tests)
			case 'D':
				settings->deamon = 1;
				break;

			// Duration
			case 'd':
				settings->duration = atoi( optarg );
				if ( settings->duration == 0 ) {
					fprintf(stderr, "Invalid duration given (%s)\n", optarg );
					return -1;
				}
				break;

			case 'i': { // min,max interations
				unsigned int min = 0, max = 0;

				if ( sscanf( optarg, "%u,%u", &min, &max ) < 2 || min == 0 || max == 0 ) {
					fprintf(stderr, "Invalid min/max (%s)\n", optarg );
					return -1;
				}

				settings->min_iterations = min;
				settings->max_iterations = max;

				break;
			}

			case 'H': { // remote host

				settings->server_host = optarg;

				break;
			}

			// Disable nagles algorithm (ie NO delay)
			case 'n':
				settings->disable_nagles = 1;
				break;

			// Parse the message size
			case 's':
				settings->message_size = atoi( optarg );
				if ( settings->message_size == 0 ) {
					fprintf(stderr, "Invalid message size given (%s)\n", optarg );
					return -1;
				}
				break;

			// Parse the message size
			case 'p':
				settings->port = atoi( optarg );
				if ( settings->port == 0 ) {
					fprintf(stderr, "Invalid port number given (%s)\n", optarg );
					return -1;
				}
				break;

			// Dirty the data
			case 'e':
				settings->dirty = 1;
				break;

			case 'T':
				settings->timestamp = 1;
				break;
			
			// Increase the verbose level
			case 'v':
				settings->verbose++;
				break;

			case 'h':
				print_usage();
				return -1;

			// TCP/UDP
			case 't':
				settings->type = SOCK_STREAM;
				settings->protocol = IPPROTO_TCP;
				break;

			case 'u':
				settings->type = SOCK_DGRAM;
				settings->protocol = IPPROTO_UDP;
				break;

			case '?':
				fprintf(stderr, "Unknown argument (%s)\n", argv[optind-1] );
				return -1;

			default:
				fprintf(stderr, "Argument not implemented (yet) (%c)\n", c );
				return -1;
		}
	}

	if ( settings->disable_nagles && settings->protocol != IPPROTO_TCP ) {
		fprintf(stderr, "Must use TCP when disabling Nagles\n" );
		return -1;
	}
	
	if( settings->timestamp && settings->message_size < sizeof(unsigned long long) ) {
		fprintf(stderr, "Message size must be greater than %u when using timestamps\n",  (unsigned int) sizeof(unsigned long long) );
		return -1;
	}

	if ( settings->deamon && optind < argc ) {
		// TODO make this test that other conflicting options haven't been needlessly set
		fprintf(stderr, "Tests can not be specified on the command line in Deamon mode\n" );
		return -1;
	}

	for (x = 0; x < cores; x++) {
		for (y = 0; y < cores; y++) {
			clientserver [ x ] [ y ] = 0;
		}
	}

	// Try and parse anything else left on the end
	// 1{0-0} 10{1-1} 3{0-1}, 1 connection core 0 to core 0, 10 connections core 1 to core 1, and 3 connections core 0 to core 1
	while (optind < argc) {
		unsigned int count; // Number of connections in this class
		unsigned int client, server; // Client and Server cores

		// Parse N{C-S}
		if ( sscanf( argv[optind], "%u{%u-%u}", &count, &client, &server ) <3 ) {
			// Check if they are using the wrong brackets
			if ( sscanf( argv[optind], "%u(%u-%u)", &count, &client, &server ) <3 ) {
				fprintf(stderr, "Unknown argument (%s)\n", argv[optind] );
				return -1;
			}
		}

		// Check all the paramters make sense
		if ( client >= cores || server >= cores ) {
			fprintf(stderr, "Cores must not be greater than %d (%s)\n", cores, argv[optind] );
			return -1;
		}

		clientserver [ client ] [ server ] += count;
		tests++;

		optind++;
	}

	// If there are no tests then error
	if ( tests == 0 ) {
		fprintf(stderr, "Please enter atleast one client/server combination\n");
		return -1;
	}

	return 0;
}

void start_daemon(const struct settings * settings) {
	//unready_threads = 0; // Number of threads not ready

	assert ( settings != NULL );

	//start_servers(&settings);
	//start_clients(&settings);

	//free(sreq); free(creq);
}

pthread_t *thread = NULL; // Array to handle thread handles
unsigned int threads = 0; // Total number of threads

int prepare_clients(const struct settings * settings) {

	unsigned int servercore, clientcore;

	assert ( settings != NULL );
	assert ( creq == NULL );
	
	// Malloc one space for each core
	creq = calloc ( cores, sizeof(*creq) );

	if ( !creq ) {
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		return -1;
	}

	// Loop through clientserver looking for each server we need to create
	for (servercore = 0; servercore < cores; servercore++) {
		for (clientcore = 0; clientcore < cores; clientcore++) {

			struct client_request_details *c;

			// Don't bother if there are zero requests
			if ( clientserver [ clientcore ] [ servercore ] == 0 )
				continue;

			// Check if we haven't set up this client thread yet
			if ( creq [ clientcore ].bRunning == 0 ) {
				creq [ clientcore ].bRunning = 1;
				creq [ clientcore ].settings = settings;
				creq [ clientcore ].core = clientcore;

				unready_threads++;
			} 

			// Malloc the request details
			c = calloc( 1, sizeof( *c ) );
			if ( !c ) {
				fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
				return -1;
			}

			// Add this new details before the other detailshttp://www.google.com/search?q=memset&ie=utf-8&oe=utf-8&aq=t&rls=org.mozilla:en-GB:official&client=firefox-a
			c->next = creq [ clientcore ].details;
			creq [ clientcore ].details = c;

			c->n = clientserver [ clientcore ] [ servercore ];
			sreq [ servercore ].n += c->n;

			// Create the client dest addr
			c->addr_len = sizeof ( struct sockaddr_in );

			c->addr = calloc ( 1, c->addr_len ) ;
			if ( !c->addr ) {
				fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
				return -1;
			}

			// Change this to be more address indepentant
			((struct sockaddr_in *)c->addr)->sin_family = AF_INET;
			((struct sockaddr_in *)c->addr)->sin_addr.s_addr = inet_addr( settings->server_host );
			((struct sockaddr_in *)c->addr)->sin_port = htons( settings->port + servercore );

			if ( ((struct sockaddr_in *)c->addr)->sin_addr.s_addr == INADDR_NONE ) {
				fprintf(stderr, "Invalid host name (%s)\n", settings->server_host );
				return -1;
			}
		}
	}

	return 0;
}

int create_clients() {
	unsigned int clientcore;

	for (clientcore = 0; clientcore < cores; clientcore++) {
		cpu_set_t cpus;

		if ( ! creq[clientcore].bRunning )
			continue;

		CPU_ZERO ( &cpus );
		CPU_SET ( clientcore, &cpus );

		if ( pthread_create_on( &thread[threads], NULL, client_thread, &creq [clientcore] , sizeof(cpus), &cpus) ) {
			fprintf(stderr, "%s:%d pthread_create_on() error\n", __FILE__, __LINE__ );
			return -1;
		}

		threads++;
	}

	return 0;
}

int prepare_servers(const struct settings * settings) {

	unsigned int servercore, clientcore;

	assert ( settings != NULL );
	assert ( sreq == NULL );
	
	// Malloc one space for each core
	sreq = calloc ( cores, sizeof(*sreq) );

	if ( !sreq ) {
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		return -1;
	}

	// Loop through clientserver looking for each server we need to create
	for (servercore = 0; servercore < cores; servercore++) {
		for (clientcore = 0; clientcore < cores; clientcore++) {

			// Don't bother if there are zero requests
			if ( clientserver [ clientcore ] [ servercore ] == 0 )
				continue;

			// Check if we haven't set up this server thread yet
			if ( sreq [ servercore ].bRunning == 0 ) {
				sreq [ servercore ].bRunning = 1;
				sreq [ servercore ].settings = settings;
				sreq [ servercore ].port = settings->port + servercore;
				sreq [ servercore ].stats.duration = settings->duration;
				sreq [ servercore ].n = 0;
				sreq [ servercore ].core = servercore;

				unready_threads++;
				server_listen_unready++;
			}
		}
	}


	return 0;
}

int create_servers() {
	unsigned int servercore;

	// Create all the server threads
	for (servercore = 0; servercore < cores; servercore++) {
		
		cpu_set_t cpus;

		// Don't bother if we don't have a server on this core
		if ( ! sreq[servercore].bRunning )
			continue;

		// Set which CPU this thread should be on
		CPU_ZERO ( &cpus );
		CPU_SET ( servercore, &cpus );

		if ( pthread_create_on( &thread[threads], NULL, server_thread, &sreq[servercore], sizeof(cpus), &cpus) ) {
			fprintf(stderr, "%s:%d pthread_create_on() error\n", __FILE__, __LINE__ );
			return -1;
		}

		threads++;
	}

	// Wait until all the servers are ready to accept connections
	while ( bRunning && server_listen_unready > 0 ) {
		usleep( 1000 );
	}

	return 0;
}

void wait_for_threads() {
	struct timespec waittime = {0, 100000000}; // 100 milliseconds

	// Spin lock until all the threads are ready
	// TODO change this to use a semaphore
	pthread_mutex_lock( &go_mutex );
	while ( bRunning && unready_threads > 0 ) {
		pthread_mutex_unlock( &go_mutex );

		pthread_mutex_lock( &ready_mutex );
		pthread_cond_timedwait( &ready_cond, &ready_mutex, &waittime);
		pthread_mutex_unlock( &ready_mutex );

		pthread_mutex_lock( &go_mutex );
	}
	// Annonce to everyone to start
	pthread_cond_broadcast( &go_cond );
	pthread_mutex_unlock( &go_mutex );
}

void cleanup_clients() {
	unsigned int i;

	if ( creq ) {
		for (i = 0; i < cores; i++) {
			struct client_request_details *c = creq[i].details;
			while ( c != NULL ) {
				struct client_request_details *nextC = c->next;
				free ( c->addr );
				free ( c );
				c = nextC;
			}
		}

		free( creq );
		creq = NULL;
	}
}

void cleanup_servers() {
	free( sreq );
	sreq = NULL;
}

void run_tests( const struct settings *settings, struct stats *total_stats ) {
	
	unsigned int i;
	unsigned int servercore;

	assert ( settings != NULL );
	assert ( total_stats != NULL );

	bRunning = 1;
	threads = 0;
	unready_threads = 0; // Number of threads not ready

	// Setup all the data for each server and client
	if ( prepare_servers(settings) )
		goto cleanup;

	if ( prepare_clients(settings) )
		goto cleanup;

	// A list of threads
	assert ( thread == NULL );
	thread = calloc( unready_threads, sizeof(*thread) );

	// Create each server/client thread
	create_servers(&settings);
	create_clients(&settings);

	wait_for_threads();

	print_headers( settings );

	// Pauses for the duration, then sets bRunning to false
	pause_for_duration( settings );

	// Block waiting until all threads die
	while (threads > 0) {
		threads--;
		pthread_join( thread[threads], NULL );
	}

	// Now sum all the results up
	i = 0;
	for (servercore = 0; servercore < cores; servercore++) {
		if ( sreq[ servercore ].port != 0 ) {
			total_stats->bytes_received += sreq[ servercore ].stats.bytes_received;
			total_stats->duration       += sreq[ servercore ].stats.duration;
			total_stats->pkts_received  += sreq[ servercore ].stats.pkts_received;
			total_stats->pkts_time  += sreq[ servercore ].stats.pkts_time;
			i++;
		}
	}

	// Divide the duration by the # of CPUs used
	total_stats->duration /= i;

cleanup:

	// Make sure we are not running anymore
	stop_all();

	// Block waiting until all threads die
	while (threads > 0) {
		threads--;
		pthread_join( thread[threads], NULL );
	}

	free(thread);
	thread = NULL;

	cleanup_clients();
	cleanup_servers();
}

int main (int argc, char *argv[]) {
	unsigned int i;

	// The sum of all the stats
	struct stats total_stats;

	// All the settings we parse
	struct settings settings;

	unsigned int iteration = 0;

	unsigned long long sum = 0;
	unsigned long long sumsquare = 0;
	unsigned long long mean = 0;
	double variance = 0.0;

	// Malloc space for a 2D array
	clientserver = calloc ( cores, sizeof(*clientserver) );
	if ( clientserver == NULL ) {
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	for (i = 0; i < cores; i++) {
		clientserver[i] = calloc ( cores, sizeof(clientserver[i]) );
		if ( clientserver[i] == NULL ) {
			fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
			goto cleanup;
		}
	}

	if ( parse_arguments( argc, argv, &settings ) ) {
		goto cleanup;
	}

#ifdef SIGPIPE
	// Disable SIGPIPE signals because they can fire from within send/read
	signal ( SIGPIPE, SIG_IGN );
#endif

#ifdef WIN32
	setup_winsock();
#endif

	memset(&total_stats, 0, sizeof(total_stats));

	// If we are daemon mode start that
	if (settings.deamon) {
		start_daemon(&settings);
		goto cleanup;
	} // Otherwise just run the test locally

	//Rerun the tests for a certain number of itterations as specified by the user
	for(iteration = 0; iteration < settings.max_iterations; iteration++) {

		// Start the tests
		run_tests( &settings, &total_stats );

		if (settings.confidence_lvl != 0) {
			sum += total_stats.bytes_received;
			sumsquare += (total_stats.bytes_received * total_stats.bytes_received);
			mean = sum / (iteration+1);
			variance = (double) (sumsquare / (iteration+1) - mean * mean);

			print_stats(sum, sumsquare, mean, variance);

			// if (interval < blah && iterations >= settings.min_iterations) {
			//	break;
			// }
		}

		print_results( &settings, -1, &total_stats );

	}

cleanup:

	if ( clientserver ) {
		for (i = 0; i < cores; ++i)
			free ( clientserver[i] );

		free( clientserver );
	}

	pthread_cond_destroy( & go_cond );
	pthread_mutex_destroy( & go_mutex );

	pthread_cond_destroy( & ready_cond );
	pthread_mutex_destroy( & ready_mutex );

	pthread_mutex_destroy( & printf_mutex );

#ifdef WIN32
	cleanup_winsock();
#endif

	return 0;
}
