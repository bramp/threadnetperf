/***
	A multi-threaded network benchmark tool
	by Andrew Brampton (2007)

	Note, this app is very rough, and needs cleaning up, but it works!
	TODO Add flag to output bandwidth at set intervals during the experiment
	TODO fix assert error when listen socket is already bound by someone else
	TODO lookup host names of address past with -H
	TODO check out ePoll
	TODO make server/client loop faster by removing unneed checks for rate limits, and others
	TODO check UDP ends without error
	TODO change the rate to be in mb/sec not packet/sec
	TODO allow it to connect to different endpoints
*/

#include "version.h"

#include "common.h"
#include "print.h"
#include "server.h"
#include "client.h"
#include "remote.h"
#include "threads.h"
#include "serialise.h"

#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>

#ifdef WIN32
	#include "getopt.h"
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

// Flag to indidcate if we can start the test
volatile int bGo = 0;

// Count of how many threads are not ready
unsigned int unready_threads = 0;

// The number of cores this machine has
const unsigned int max_cores = 8; // TODO get the real number!

int null_func(const struct settings *settings, void * data) { return 0; };
int null_func2(struct settings *settings, void ** data) { return 0; };

// List of functions which run() uses
struct run_functions {
	int (*setup)(struct settings *, void ** data);

	int (*prepare_servers)(const struct settings *, void *);
	int (*prepare_clients)(const struct settings *, void *);

	int (*create_servers)(const struct settings *, void *);
	int (*create_clients)(const struct settings *, void *);

	int (*wait_for_go)(const struct settings *, void *);

	int (*print_headers)(const struct settings *, void *);

	int (*collect_results)(const struct settings *, struct stats *, int (*printer)(const struct settings *, const struct stats *, void *), void *);
	int (*print_results)(const struct settings *, const struct stats *, void *);

	int (*cleanup)(const struct settings *, void *);
};

// The run sequence for a local test
struct run_functions local_funcs = {
	null_func2,            //setup
	prepare_servers,       //prepare_servers
	prepare_clients,       //prepare_clients
	create_servers,        //create_servers
	create_clients,        //create_clients
	null_func,             //wait_for_go
	print_headers,         //print_headers
	thread_collect_results,//collect_results
	print_results,         //print_results
	null_func,             //cleanup
};

// The run sequence for a remote server
struct run_functions remote_server_funcs = {
	remote_accept,         //setup
	prepare_servers,       //prepare_servers
	null_func,             //prepare_clients
	create_servers,        //create_servers
	signal_ready,          //create_clients
	signal_go,             //wait_for_go
	null_func,             //print_headers
	thread_collect_results,//collect_results
	remote_send_results,   //print_results
	remote_cleanup         //cleanup
};

// The run sequence for a client (connecting to a remote server)
struct run_functions remote_client_funcs = {
	remote_connect,            //setup
	null_func,                 //prepare_servers
	prepare_clients,           //prepare_clients
	wait_ready,                //create_servers
	create_clients,            //create_clients
	wait_go,                   //wait_for_go
	print_headers,         //print_headers
	remote_collect_results,    //collect_results
	print_results,             //print_results
	remote_cleanup             //cleanup
};

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

void print_version() {
	fprintf(stderr, "threadnetperf r%s by bramp 2007-2008\n", THREADNETPERF_VERSION );
}

void print_usage() {

	print_version();
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
	fprintf(stderr, "	-T         Timestamp packets, and measure latency (only available on *nix)\n" );
	fprintf(stderr, "	-t         Use TCP\n" );
	fprintf(stderr, "	-r         Packets per second rate (default: ~0)\n" );
	fprintf(stderr, "	-u         Use UDP\n" );
	fprintf(stderr, "	-v         Verbose\n" );
	fprintf(stderr, "	-V         Display version only\n" );

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
	const char *optstring = "DhvVtTeuns:d:p:c:i:H:r:";

	assert ( settings != NULL );

	// Default arguments
	settings->deamon = 0;
	settings->message_size = 1024;
	settings->socket_size = ~0;
	settings->rate = ~0;
	settings->disable_nagles = 0;
	settings->duration = 10;
	settings->server_host = "127.0.0.1";
	settings->port = 1234;
	settings->verbose = 0;
	settings->dirty = 0;
	settings->timestamp = 0;
	settings->confidence_lvl = 0.0;
	settings->confidence_int = 0.0;
	settings->min_iterations = 1;
	settings->max_iterations = 1;

	settings->type = SOCK_STREAM;
	settings->protocol = IPPROTO_TCP;

	if ( argc == 1 ) {
		print_usage();
		return -1;
	}

	// A first pass of getopt to work out if we are a Deamon
	while ((c = getopt(argc, argv, optstring)) != -1) {
		switch ( c ) {

			// Deamon mode (wait for incoming tests)
			case 'D':
				settings->deamon = 1;
				break;

			case 'h':
				print_usage();
				return -1;

			// Increase the verbose level
			case 'v':
				settings->verbose = 1;
				break;

			case 'V':
				print_version();
				return -1;

			case ':':
				fprintf(stderr, "Missing argument for (%s)\n", argv[optind-1] );
				return -1;

			case '?':
				fprintf(stderr, "Unknown argument (%s)\n", argv[optind-1] );
				return -1;

			default:
				break;
		}
	}

	if ( settings->deamon && optind < argc ) {
		fprintf(stderr, "Tests can not be specified on the command line in Deamon mode\n" );
		return -1;
	}

	optind = 0;

	// Second pass which actually does the work
	while ((c = getopt(argc, argv, optstring)) != -1) {
		switch ( c ) {

			case 'c': {
				double level = 95.0, interval = 5.0;

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to set confidence interval when in Deamon mode\n");
					return -1;
				}

				if ( sscanf( optarg, "%lf,%lf", &level, &interval ) < 2 ) {
					fprintf(stdout, "%lf%% Confidence interval defaulted to %lf percent\n", level, interval);
				}

				if (level != 75.0 && level != 90.0 && level != 95.0 && level != 97.5 && 
					level != 99.0 && level != 99.5 && level != 99.95) {
					fprintf(stderr, "Confidence Level must be {75, 90, 95, 97.5, 99, 99.5, 99.95}. Given (%s)\n", optarg);
					return -1;
				}

				settings->confidence_lvl = level;
				settings->confidence_int = interval;

				break;
			}


			// Duration
			case 'd':

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to set duration when in Deamon mode\n");
					return -1;
				}

				settings->duration = atoi( optarg );
				if ( settings->duration == 0 ) {
					fprintf(stderr, "Invalid duration given (%s)\n", optarg );
					return -1;
				}

				break;

			case 'i': { // min,max interations
				unsigned int min = 0, max = 0;

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to set iterations when in Deamon mode\n");
					return -1;
				}

				if ( sscanf( optarg, "%u,%u", &min, &max ) < 2 || min > max || max == 0 ) {
					fprintf(stderr, "Invalid min/max iterations(%s)\n", optarg );
					return -1;
				}
				settings->min_iterations = min;
				settings->max_iterations = max;

				break;
			}

			case 'H': { // remote host

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to set remote host when in Deamon mode\n");
					return -1;
				}

				settings->server_host = optarg;
				break;
			}

			// Disable nagles algorithm (ie NO delay)
			case 'n':

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to disable Nagles when in Deamon mode\n");
					return -1;
				}

				settings->disable_nagles = 1;
				break;

			// Parse the message size
			case 's':

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to set message size when in Deamon mode\n");
					return -1;
				}

				settings->message_size = atoi( optarg );
				if ( settings->message_size == 0 ) {
					fprintf(stderr, "Invalid message size given (%s)\n", optarg );
					return -1;
				}
				break;

			// Send rate
			case 'r':

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to set send rate when in Deamon mode\n");
					return -1;
				}

				settings->rate = atoi( optarg );
				if ( settings->rate == 0 ) {
					fprintf(stderr, "Invalid send rate given (%s)\n", optarg );
					return -1;
				}
				break;

			// Parse the port
			case 'p':

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to set port when in Deamon mode\n");
					return -1;
				}

				settings->port = atoi( optarg );
				if ( settings->port == 0 ) {
					fprintf(stderr, "Invalid port number given (%s)\n", optarg );
					return -1;
				}
				break;

			// Dirty the data
			case 'e':

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to eat the data when in Deamon mode\n");
					return -1;
				}

				settings->dirty = 1;
				break;

			case 'T':

#ifdef WIN32
				fprintf(stdout, "Timestamps option unavailable on windows\n");
				return -1;
#endif

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to set timestamps when in Deamon mode\n");
					return -1;
				}

				settings->timestamp = 1;
				break;

			// TCP/UDP
			case 't':

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to set TCP when in Deamon mode\n");
					return -1;
				}

				settings->type = SOCK_STREAM;
				settings->protocol = IPPROTO_TCP;
				break;

			case 'u':

				if ( settings->deamon ) {
					fprintf(stdout, "Unable to set UDP when in Deamon mode\n");
					return -1;
				}

				settings->type = SOCK_DGRAM;
				settings->protocol = IPPROTO_UDP;
				break;

			// Ignore the following parameters as they have been parsed in a previous getopt loop
			case 'D': case 'h': case 'v': case 'V':
				break;

			default:
				fprintf(stderr, "Argument not implemented (yet) (%c)\n", c );
				return -1;
		}
	}

	if ( settings->disable_nagles && settings->protocol != IPPROTO_TCP ) {
		fprintf(stderr, "Must use TCP when disabling Nagles\n" );
		return -1;
	}

//	if( settings->timestamp && settings->message_size < sizeof(unsigned long long) ) {
//		fprintf(stderr, "Message size must be greater than %u when using timestamps\n",  (unsigned int) sizeof(unsigned long long) );
//		return -1;
//	}

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

	return 0;
}

/*
// Parses N(C-S)
int parse_combination( const char *arg ) {
	if ( sscanf( arg, "%u", &count ) <1 ) {
		goto parse_error;
	}

	if ( sscanf( arg, "(", &count ) <1 ) {
		if ( sscanf( arg, "{", &count ) <1 ) {
			goto parse_error;
		}
	}

	if ( sscanf( argv[optind], "%u", &client ) <3 ) {
		// Check if they are using the ?
		if ( sscanf( arg, "?-)", &count ) <3 ) {

		}
	}

		if ( sscanf( arg, "{", &count ) <1 ) {
			goto parse_error;
		}

	if ( sscanf( arg, ")", &count ) <1 ) {
		if ( sscanf( arg, "}", &count ) <1 ) {
			goto parse_error;
		}
	}

	return 0;

parse_error:
	fprintf(stderr, "Unknown argument (%s)\n", arg );
	return -1;
}
*/

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
	bGo = 1;
	pthread_cond_broadcast( &go_cond );
	pthread_mutex_unlock( &go_mutex );
}

void run( const struct run_functions * funcs, struct settings *settings, struct stats *total_stats ) {

	void *data = NULL;

	assert ( funcs != NULL );
	assert ( settings != NULL );
	assert ( total_stats != NULL );

	bGo = 0;
	bRunning = 1;
	unready_threads = 0; // Number of threads not ready
	threads_clear();

	if ( funcs->setup ( settings, &data ) ) {
		fprintf(stderr, "%s:%d prepare_servers() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Setup all the data for each server and client
	if ( funcs->prepare_servers(settings, data) ) {
		fprintf(stderr, "%s:%d prepare_servers() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	if ( funcs->prepare_clients(settings, data) ) {
		fprintf(stderr, "%s:%d prepare_clients() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	assert ( unready_threads > 0 );

	// A list of threads
	if ( thread_alloc(unready_threads) ) {
		fprintf(stderr, "%s:%d thread_alloc() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Create each server/client thread
	if ( funcs->create_servers(settings, data) ) {
		fprintf(stderr, "%s:%d create_servers() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	if ( funcs->create_clients(settings, data) ) {
		fprintf(stderr, "%s:%d create_clients() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Wait for our threads to be created
	wait_for_threads();

	if ( funcs->wait_for_go(settings, data) ) {
		fprintf(stderr, "%s:%d wait_for_go() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Wait and then signal a go!
	start_threads();

	// Pauses for the duration
	pause_for_duration( settings );

	stop_all();

	if ( funcs->print_headers(settings, data) ) {
		fprintf(stderr, "%s:%d print_headers() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	if ( funcs->collect_results ( settings, total_stats, funcs->print_results, data) ) {
		fprintf(stderr, "%s:%d collect_results() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

cleanup:

	// Make sure we are not running anymore
	stop_all();

	thread_join_all();
	threads_clear();

	cleanup_clients();
	cleanup_servers();

	funcs->cleanup( settings, data );
}

void run_deamon(const struct settings *settings) {

	assert ( settings != NULL );

	start_daemon(settings);

	// Now loop accepting incoming tests
	while ( 1 ) {
		struct settings remote_settings;
		struct stats total_stats;

		if ( settings->verbose ) {
			printf("Waiting for test...\n");
		}

		run( &remote_server_funcs, &remote_settings, &total_stats );
	}

	close_daemon();
}

int main (int argc, char *argv[]) {

	// The sum of all the stats
	struct stats total_stats;

	// All the settings we parse
	struct settings settings;
	struct run_functions *funcs = NULL;

	unsigned int iteration = 0;
	double sum = 0.0;
	double sumsquare = 0.0;

	settings.cores = max_cores;
	settings.clientserver = (unsigned int **)malloc_2D(sizeof(unsigned int), settings.cores, settings.cores);

	if ( parse_arguments( argc, argv, &settings ) ) {
		goto cleanup;
	}

#ifdef WIN32
	setup_winsock();
#endif

#ifdef SIGPIPE
	// Disable SIGPIPE signals because they can fire from within send/read
	signal ( SIGPIPE, SIG_IGN );
#endif

	// If we are daemon mode start that
	if (settings.deamon) {
		run_deamon(&settings);
		goto cleanup;
	}

	// Decide what kind of test this is
	// TODO do a better test for localhost
	if ( strcmp(settings.server_host, "127.0.0.1") != 0 ) {
		funcs = &remote_client_funcs;
	} else {
		funcs = &local_funcs;
	}

	//Rerun the tests for a certain number of iterations as specified by the user
	for(iteration = 0; iteration < settings.max_iterations; iteration++) {

		memset(&total_stats, 0, sizeof(total_stats));
		total_stats.core = ~0;

		// Start the tests
		run ( funcs, &settings, &total_stats );

		// Print the results
		funcs->print_results( &settings, &total_stats, NULL );

		if (settings.confidence_lvl != 0.0 ) {
			// Only calculate after we reached the min
			if ( iteration >= settings.min_iterations ) {
				double mean;
				double variance;
				double conf_interval;

				sum += total_stats.bytes_received;
				sumsquare += total_stats.bytes_received * total_stats.bytes_received;
				mean = sum / (iteration+1);
				variance = (double)(sumsquare / (iteration+1) - mean * mean);

				if(settings.verbose)
					print_stats(sum, sumsquare, mean, variance);

				conf_interval = calc_confidence(settings.confidence_lvl, mean, variance, iteration+1, settings.verbose);
				if ( (conf_interval < (settings.confidence_int/100) * mean) ) {
					break;
				}
			}
		} 
	}

cleanup:

	free_2D( (void **)settings.clientserver, settings.cores);

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
