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
#include "client.h"
#include "daemon.h"

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
volatile int bRunning = 1;

// Count of how many threads are not ready
unsigned int unready_threads = 0;

// The number of cores this machine has
const unsigned int max_cores = 8; // TODO get the real number!

 // Array to handle thread handles
pthread_t *thread = NULL;

 // Total number of threads
size_t threads = 0;



// Signals all threads to stop
void stop_all () {

	bRunning = 0;

	stop_all_clients();
	stop_all_servers();
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

		if ( now >= end_time ) {
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

	fprintf(stderr, "	-c level,interval   Confidence level, must be 95 or 99\n");
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
	settings->confidence_int = 0;
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

			case 'c': {
				double level = 0.0, interval = 0.0;
				
				if ( sscanf( optarg, "%lf,%lf", &level, &interval ) < 2 ) {
					fprintf(stdout, "Confidence interval defaulted to 5percent\n");
				}
				
				if((level != 75 && level !=90 && level != 95 && level != 97.5 && level != 99 && level != 99.5 && level != 99.95 ) ) {
						fprintf(stderr, "Confidence Level must be {75,90,95,97.5,99,99.5,99.95}. Given (%s)\n", optarg);
						return -1;
					}

				settings->confidence_lvl = level ;
				settings->confidence_int = interval ;
				break;
			}
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

			// Parse the message size->
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
				settings->verbose = 1;
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
		fprintf(stderr, "Tests can not be specified on the command line in D->eamon mode\n" );
		return -1;
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
		// TODO check if the server is remote, and then decide if the cores make sense
		if ( client >= max_cores || server >= max_cores ) {
			fprintf(stderr, "Cores must not be greater than %d (%s)\n", max_cores, argv[optind] );
			return -1;
		}

		settings->clientserver [ client ] [ server ] += count;
		tests++;

		optind++;
	}

	// If there are no tests then error
	if ( tests == 0 && !settings->deamon ) {
		fprintf(stderr, "No tests were specified\n");
		return -1;
	}

	if ( tests != 0 && settings->deamon ) {
		fprintf(stderr, "Cannot specify tests while running as a deamon\n");
		return -1;
	}

	return 0;
}

// Wait until every thread signals a ready
void wait_for_threads() {
	struct timespec waittime = {0, 100000000}; // 100 milliseconds

	pthread_mutex_lock( &go_mutex );
	while ( bRunning && unready_threads > 0 ) {
		pthread_mutex_unlock( &go_mutex );

		pthread_mutex_lock( &ready_mutex );
		pthread_cond_timedwait( &ready_cond, &ready_mutex, &waittime);
		pthread_mutex_unlock( &ready_mutex );

		pthread_mutex_lock( &go_mutex );
	}
	pthread_mutex_unlock( &go_mutex );
}

// Annonce to everyone to start
void start_threads() {
	pthread_mutex_lock( &go_mutex );
	pthread_cond_broadcast( &go_cond );
	pthread_mutex_unlock( &go_mutex );
}

// Join all these threads
size_t threads_join(const pthread_t *threads, size_t count) {
	while (count > 0) {
		count--;
		pthread_join( threads[count], NULL );
	}
	return count;
}

size_t threads_sum_stats(const pthread_t *threads, size_t count, const struct settings *settings, struct stats *total_stats) {
	unsigned int i = 0;

	while (count > 0) {
		struct stats *stats;

		count--;
		pthread_join( threads[count], (void **)&stats );

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

	return count;
}

// Runs tests locally creating both servers and clients
void local_run_tests( const struct settings *settings, struct stats *total_stats ) {

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
	if ( !thread ) {
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Create each server/client thread
	if ( create_servers(&settings) )
		goto cleanup;

	if ( create_clients(&settings) )
		goto cleanup;

	// Wait and then signal a go!
	wait_for_threads();
	start_threads();

	// Pauses for the duration, then sets bRunning to false
	pause_for_duration( settings );

	stop_all();

	print_headers( settings );

	// Block waiting until all threads die and print out their stats
	threads = threads_sum_stats(thread, threads, settings, total_stats);

cleanup:

	// Make sure we are not running anymore
	stop_all();

	threads_join(thread, threads);

	free(thread);
	thread = NULL;

	cleanup_clients();
	cleanup_servers();
}

void run_remote(const struct settings *settings) {
	SOCKET s = INVALID_SOCKET;
	struct stats total_stats;

	bRunning = 1;
	threads = 0;
	unready_threads = 0; // Number of threads not ready

	s = connect_daemon(settings);
	if ( s == INVALID_SOCKET )
		goto cleanup;

	if ( send_test( s, settings) )
		goto cleanup;

	if ( prepare_clients(settings) )
		goto cleanup;

	// A list of threads
	assert ( thread == NULL );
	thread = calloc( unready_threads, sizeof(*thread) );
	if ( !thread ) {
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Wait for a threads
	if ( wait_ready(s) )
		goto cleanup;

	// Now start connecting the clients
	if ( create_clients(&settings) )
		goto cleanup;

	wait_for_threads();

	// Wait for a go from the server
	if ( wait_go(s) )
		goto cleanup;

	// Now go
	start_threads();

	// Pauses for the duration, then sets bRunning to false
	pause_for_duration( settings );

	stop_all();

	// recv results

cleanup:
	// Make sure we are not running anymore
	stop_all();

	threads_join(thread, threads);

	free(thread);
	thread = NULL;

	cleanup_clients();
}

void run_deamon(const struct settings *settings) {

	SOCKET listen_socket = INVALID_SOCKET;

	assert ( settings != NULL );

	listen_socket = start_daemon(settings);

	// Now loop accepting incoming tests
	while ( 1 ) {
		struct settings remote_settings;
		SOCKET s = INVALID_SOCKET;
		struct stats total_stats;

		bRunning = 1;
		threads = 0;
		unready_threads = 0; // Number of threads not ready

		// Wait for a test to come in
		s = accept_test( listen_socket, &remote_settings, settings->verbose );
		if ( s == INVALID_SOCKET )
			goto main_cleanup;

		remote_settings.verbose = 0; // Make sure verbose is off on the server

		// Setup all the data for each server
		if ( prepare_servers(&remote_settings) )
			goto cleanup;

		// A list of threads
		assert ( thread == NULL );
		thread = calloc( unready_threads, sizeof(*thread) );
		if ( !thread ) {
			fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
			goto cleanup;
		}

		// Create each server/client thread
		if ( create_servers(&remote_settings) )
			goto cleanup;

		// Signal the the remote machine that the servers are ready
		if ( signal_ready ( s ) )
			goto cleanup;

		// Wait for a go
		wait_for_threads();
		
		// Signal the remote machine that the clients are all connected
		if ( signal_go ( s ) )
			goto cleanup;

		// And now tell our servers to go!
		start_threads();

		// Pause until the remote end starts to disconnect

		// Stop
		stop_all();

		// Block waiting until all threads die and print out their stats
		threads = threads_sum_stats(thread, threads, settings, &total_stats);

	cleanup:

		// Make sure we are not running anymore
		stop_all();

		threads_join(thread, threads);

		free(thread);
		thread = NULL;

		cleanup_servers();
	}

main_cleanup:

	close_daemon(listen_socket);
}

int main (int argc, char *argv[]) {

	// The sum of all the stats
	struct stats total_stats;

	// All the settings we parse
	struct settings settings;

	unsigned int iteration = 0;
	double sum = 0.0;
	double sumsquare = 0.0;

	settings.cores = max_cores;
	settings.clientserver = (unsigned int **)malloc_2D(sizeof(unsigned int), settings.cores, settings.cores);

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

	// If we are daemon mode start that
	if (settings.deamon) {
		run_deamon(&settings);
		goto cleanup;
	} 
	// Otherwise just run the test locally

	// Some test code to aid in debugging
	if ( strcmp(settings.server_host, "127.0.0.1") != 0 ) {
		run_remote(&settings);
		goto cleanup;
	}

	//Rerun the tests for a certain number of iterations as specified by the user
	for(iteration = 0; iteration < settings.max_iterations; iteration++) {

		memset(&total_stats, 0, sizeof(total_stats));
		total_stats.core = -1;

		// Start the tests
		local_run_tests( &settings, &total_stats );

		if (settings.confidence_lvl != 0.0) {
			double mean;
			double variance;
			double confidence_interval;

			sum += total_stats.bytes_received;
			sumsquare += (total_stats.bytes_received * total_stats.bytes_received);
			mean = sum / (iteration+1);
			variance = (double)(sumsquare / (iteration+1) - mean * mean);

			if(settings.verbose) 
				print_stats(sum, sumsquare, mean, variance);

			confidence_interval = calc_confidence(settings.confidence_lvl, mean, variance, iteration+1, settings.verbose);
			if ( (confidence_interval < (settings.confidence_int/100) * mean) && iteration >= settings.min_iterations) {
				print_results( &settings, &total_stats );
				break;
			}
		}

		print_results( &settings, &total_stats );
	}

cleanup:

	free_2D( (void **)settings.clientserver, settings.cores, settings.cores);

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
