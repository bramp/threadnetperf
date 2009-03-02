/***
	A multi-threaded network benchmark tool
	by Andrew Brampton (2007-2009)

	Note, this app is very rough, and needs cleaning up, but it works!
	TODO Add flag to output bandwidth at set intervals during the experiment
	TODO fix assert error when listen socket is already bound by someone else
	TODO make server/client loop faster by removing unneed checks for rate limits, and others
	TODO check UDP ends without error
	TODO change the rate to be in mb/sec not packet/sec
	TODO allow it to connect to different endpoints
	TODO Allow the connections to mimic BitTorrent/HTTP/NNTP/IRC/etc
	TODO Add the error msg "server_thread() error Server thread can have no more than %d connections (%d specified)\n"
	TODO Output time taken to set-up
	TODO Change AF_UNIX to socketpair isntead of socket (TODO don't use tmpnam())
	TODO set sock option in receive stats to wait for a whole struct per recv
	TODO Don't send verbose across the network
	TODO both thread_collect_results and remote_collect_results look very similar
*/

#include "common.h"
#include "print.h"
#include "server.h"
#include "client.h"
#include "remote.h"
#include "threads.h"
#include "serialise.h"
#include "parse.h"

#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>

#ifndef WIN32
#include <unistd.h> //usleep
#endif


// Condition Variable to signal when ready to accept
static pthread_cond_t ready_to_accept_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t ready_to_accept_mtx = PTHREAD_MUTEX_INITIALIZER;

// Condition Variable to signal when we are ready to go
static pthread_cond_t ready_to_go_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t ready_to_go_mtx = PTHREAD_MUTEX_INITIALIZER;

// Condition Variable to signal when we should go
pthread_cond_t go_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t go_mutex = PTHREAD_MUTEX_INITIALIZER;

// Flag to indidcate if we are still running
volatile int bRunning = 1;

// Flag to indidcate if we can start the test
int bGo = 0;

// Count of how many threads are not ready
int unready_threads = 0;

int server_listen_unready = 0;

int null_func(const struct settings *settings, void * data) { return 0; };
int null_func2(struct settings *settings, void ** data) { return 0; };
int setup(struct settings *, void ** data);

extern char* ipc_sock_name;

// List of functions which run() uses
struct run_functions {
	int (*setup)(struct settings *, void ** data);

	int (*prepare_servers)(const struct settings *, void *);
	int (*prepare_clients)(const struct settings *, void *);

	// returns the number of servers created or negative on error.
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
	setup,                 //setup
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
void stop_all (unsigned int threaded_model) {
	//Change bRunning here?
	bRunning = 0;
	threads_signal_all(SIGNAL_STOP, threaded_model);
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

void signal_handler(int sig, siginfo_t *siginfo, void* context) {
	union sigval param = siginfo->si_value;

	assert (sig == SIGNUM );

	switch(param.sival_int) {
		//Received by controller
		case SIGNAL_READY_TO_ACCEPT :
			printf("(%d) Received SIGNAL_READY_TO_ACCEPT server_listen_unready %d\n",getpid(), server_listen_unready);
			pthread_mutex_lock( &ready_to_accept_mtx );
			server_listen_unready--;
			assert ( server_listen_unready >= 0 );
			printf("(%d) obtained the SIGNAL_READY_TO_ACCEPT lock\n",getpid());
			if ( server_listen_unready == 0 ) {
				pthread_cond_broadcast( &ready_to_accept_cond );
			}
			pthread_mutex_unlock( &ready_to_accept_mtx );
			break;
		//Received by controller
		case SIGNAL_READY_TO_GO :
			printf("(%d) Received SIGNAL_READY_TO_GO unready_threads %d\n",getpid(),unready_threads);
			pthread_mutex_lock( &ready_to_go_mtx );
			unready_threads--;
			printf("(%d) obtained the SIGNAL_READY_TO_GO lock\n",getpid());
			assert ( unready_threads >= 0 );
			if ( unready_threads == 0 ) {
				pthread_cond_broadcast( &ready_to_go_cond );
			}
			pthread_mutex_unlock( &ready_to_go_mtx );
			break;
		//Received by server threads (start_threads)
		case SIGNAL_GO :
			printf("(%d) Received SIGNAL_GO\n", getpid());
			pthread_mutex_lock( &go_mutex );
			printf("(%d) obtained the SIGNAL_GO lock\n", getpid());
			bGo = 1;
			pthread_cond_broadcast( &go_cond );
			pthread_mutex_unlock( &go_mutex );
			break;
		//Received by server threads
		case SIGNAL_STOP:
			printf("(%d) Received SIGNAL_STOP\n", getpid());
			bRunning = 0;
			break;
		default :
			fprintf(stderr, "%s:%d signal_handler() unknown sigval %d:%d\n", __FILE__, __LINE__, sig, param.sival_int );
			break;
	}
}

/* 
 * Setup our signal handler to point to signal_handler
 * - Make sure we use the right signal handler
 * @param - signum is the signal number we want to listen for
 */
int setup_signals(int signum)	{
   struct sigaction act;
   memset(&act, 0, sizeof(act));

   act.sa_sigaction     = signal_handler;
   act.sa_flags         = SA_SIGINFO;
   sigemptyset(&act.sa_mask);
   //siginterrupt(signum, 0);
   return sigaction(signum, &act, NULL);
}

// Wait for a condition to be true
void wait_for( pthread_mutex_t* mutex, pthread_cond_t* cond, int* cond_variable ) {
	pthread_mutex_lock( mutex );
	while ( bRunning && *cond_variable > 0 ) {
		struct timespec abstime;

		get_timespec_now(&abstime);
		abstime.tv_sec += 1;
		pthread_cond_timedwait( cond, mutex, &abstime);
	}
	pthread_mutex_unlock( mutex );
}

// Annonce to everyone to start
void start_threads(unsigned int threaded_model) {
	threads_signal_all(SIGNAL_GO, threaded_model);
}

int setup(struct settings * settings, void ** data) {
	return remote_setup_data( data, INVALID_SOCKET);
}

void run( const struct run_functions * funcs, struct settings *settings, struct stats *total_stats ) {

	void *data = NULL;
	int	server_threads = 0;
	int	client_threads = 0;

	assert ( funcs != NULL );
	assert ( settings != NULL );
	assert ( total_stats != NULL );

	bGo = 0;
	bRunning = 1;

	threads_clear();

	if ( funcs->setup ( settings, &data ) ) {
		fprintf(stderr, "%s:%d setup() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Setup all the data for each server and client
	server_threads = funcs->prepare_servers(settings, data);
	if ( server_threads < 0  ) {
		fprintf(stderr, "%s:%d prepare_servers() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}
	
	printf("A\n");
	pthread_mutex_lock ( &ready_to_accept_mtx );
	server_listen_unready = server_threads;
	pthread_mutex_unlock ( &ready_to_accept_mtx );

	client_threads = funcs->prepare_clients(settings, data);
	if ( client_threads < 0 ) {
		fprintf(stderr, "%s:%d prepare_clients() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}
	
	pthread_mutex_lock( &ready_to_go_mtx );
	unready_threads	= server_threads + client_threads;
	
	
	// A list of threads
	if ( thread_alloc(unready_threads) ) {
		fprintf(stderr, "%s:%d thread_alloc() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}
	pthread_mutex_unlock( &ready_to_go_mtx );

	printf("B\n");
	// Create each server/client thread
	if ( funcs->create_servers(settings, data) ) {
		fprintf(stderr, "%s:%d create_servers() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	wait_for( &ready_to_accept_mtx, &ready_to_accept_cond, &server_listen_unready);

	printf("C\n");
	if ( funcs->create_clients(settings, data) ) {
		fprintf(stderr, "%s:%d create_clients() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Wait for our threads to be created
	wait_for( &ready_to_go_mtx, &ready_to_go_cond, &unready_threads);

	printf("D\n");
	if ( funcs->wait_for_go(settings, data) ) {
		fprintf(stderr, "%s:%d wait_for_go() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Wait and then signal a go!
	start_threads(settings->threaded_model);

	// Pauses for the duration
	pause_for_duration( settings );

	stop_all(settings->threaded_model);
	printf("E\n");

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
	stop_all(settings->threaded_model);

	//If we don't join here, then we may leave zombie processes
	//Zombies like brains.
	thread_join_all(settings->threaded_model);

	unlink(ipc_sock_name);
	free(ipc_sock_name);

	//thread_join_all(settings->threaded_model);
	threads_clear();

	cleanup_clients();
	cleanup_servers();

	closesocket(((struct remote_data *)data)->stats_socket);

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
		free( remote_settings.test );
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

	printf("threadnetperf started\n");
#ifdef WIN32
	setup_winsock();
#endif

#ifdef SIGPIPE
	// Disable SIGPIPE signals because they can fire from within send/read
	signal ( SIGPIPE, SIG_IGN );
#endif

	if ( parse_settings( argc, argv, &settings ) ) {
		goto cleanup;
	}

	if ( setup_signals(SIGNUM)) {
		fprintf(stderr, "%s:%d setup_signals() error %d\n", __FILE__, __LINE__ , errno);
		goto cleanup;
	}
	
	printf("threadnetperf about to get going\n");

	// If we are daemon mode start that
	if (settings.deamon) {
		run_deamon(&settings);
		goto cleanup;
	}

	// Decide what kind of test this is
	// TODO do a better test for localhost
	if ( settings.server_host != NULL ) {
		funcs = &remote_client_funcs;
	} else {
		funcs = &local_funcs;
	}

	//Rerun the tests for a certain number of iterations as specified by the user
	for(iteration = 0; iteration < settings.max_iterations; iteration++) {

		memset(&total_stats, 0, sizeof(total_stats));
		total_stats.cores = 0;

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

	free( settings.test );

	pthread_cond_destroy( & go_cond );
	pthread_mutex_destroy( & go_mutex );

	pthread_cond_destroy( & ready_to_go_cond );
	pthread_mutex_destroy( & ready_to_go_mtx );

	pthread_mutex_destroy( & printf_mutex );

#ifdef WIN32
	cleanup_winsock();
#endif

	return 0;
}
