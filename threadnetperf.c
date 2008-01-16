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

// Condition Variable that is signed each time a thread is ready
pthread_cond_t ready_cond;

// Condition Variable to indicate when all the threads are connected and ready to go
pthread_cond_t go_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t go_mutex = PTHREAD_MUTEX_INITIALIZER;


// Flag to indidcate if we are still running
volatile int bRunning = 1;

// Count of how many threads are ready
volatile unsigned int unready_threads = 0;

// The number of cores this machine has
const unsigned int cores = 8; // TODO get the read number!

struct server_request *sreq = NULL;
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
	long long duration = settings->duration * 1000000;

	// This main thread controls when the test ends
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

	fprintf(stderr, "	-c         Confidence level, must be 95 or 99\n");
	fprintf(stderr, "	-D         Use deamon mode (wait for incoming tests)\n" );
	fprintf(stderr, "	-d time    Set duration to run the test for\n" );
	fprintf(stderr, "	-e         Eat the data (i.e. dirty it)\n");
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

	// Default arguments
	settings->deamon = 0;
	settings->message_size = 1024;
	settings->socket_size = -1;
	settings->disable_nagles = 0;
	settings->duration = 10;
	settings->port = 1234;
	settings->verbose = 0;
	settings->dirty = 0;
	settings->timestamp = 0;
	settings->confidence_lvl = 0;

	settings->type = SOCK_STREAM;
	settings->protocol = IPPROTO_TCP;

	if ( argc == 1 ) {
		print_usage();
		return -1;
	}

	// Lets parse some command line args
	while ((c = getopt(argc, argv, "cDtTeunvhs:d:p:")) != -1) {
		switch ( c ) {
			//confidence level, must be either 95 or 99
			case 'c':
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

	for (x = 0; x < cores; x++) {
		for (y = 0; y < cores; y++) {
			clientserver [ x ] [ y ] = 0;
		}
	}

	if ( settings->deamon && optind < argc ) {
		fprintf(stderr, "Tests can not be specified on the command line in Deamon mode\n" );
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
		if ( client >= cores || server >= cores ) {
			fprintf(stderr, "Cores must not be greater than %d (%s)\n", cores, argv[optind] );
			return -1;
		}

		clientserver [ client ] [ server ] += count;

		optind++;
	}

	return 0;
}

int main (int argc, char *argv[]) {
	pthread_t *thread = NULL; // Array to handle thread handles
	unsigned int threads = 0; // Total number of threads
	unsigned int i;
	int ret;
	unsigned int servercore, clientcore;

	// The sum of all the stats
	struct stats total_stats = {0,0,0};

	// All the settings we parse
	struct settings settings;

#ifdef WIN32
	setup_winsock();
#endif
	
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

	print_headers( &settings );
	
	// Malloc one space for each core
	sreq = calloc ( cores, sizeof(*sreq) );
	creq = calloc ( cores, sizeof(*creq) );

	if ( sreq == NULL || creq == NULL  ) {
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Number of threads not ready
	unready_threads = 0;

	// Loop through clientserver looking for each set of connections we need to create
	for (servercore = 0; servercore < cores; servercore++) {
		for (clientcore = 0; clientcore < cores; clientcore++) {

			struct client_request_details *c;

			// Don't bother if there are zero requests
			if ( clientserver [ clientcore ] [ servercore ] == 0 )
				continue;

			// Check if we haven't set up this server thread yet
			if ( sreq [ servercore ].bRunning == 0 ) {
				sreq [ servercore ].bRunning = 1;
				sreq [ servercore ].settings = &settings;
				sreq [ servercore ].port = settings.port + servercore;
				sreq [ servercore ].stats.duration = settings.duration;
				sreq [ servercore ].n = 0;
				sreq [ servercore ].core = servercore;

				unready_threads++;
			}

			// Check if we haven't set up this client thread yet
			if ( creq [ clientcore ].bRunning == 0 ) {
				creq [ clientcore ].bRunning = 1;
				creq [ clientcore ].settings = &settings;
				creq [ clientcore ].core = clientcore;
				unready_threads++;
			} 

			// Malloc the request details
			c = calloc( 1, sizeof( *c ) );

			// Add this new details before the other details
			c->next = creq [ clientcore ].details;
			creq [ clientcore ].details = c;

			c->n = clientserver [ clientcore ] [ servercore ];
			sreq [ servercore ].n += c->n;

			// Create the client dest addr
			c->addr_len = sizeof ( struct sockaddr_in );

			c->addr = calloc ( 1, c->addr_len ) ;

			((struct sockaddr_in *)c->addr)->sin_family = AF_INET;
			((struct sockaddr_in *)c->addr)->sin_addr.s_addr = inet_addr( "127.0.0.1" );
			((struct sockaddr_in *)c->addr)->sin_port = htons( settings.port + servercore );

		}
	}

	// If there are no paramters then error
	if ( unready_threads == 0 ) {
		fprintf(stderr, "Please enter atleast one client/server combination\n");
		goto cleanup;
	}

	// A list of threads
	thread = calloc( unready_threads, sizeof(*thread) );

	// Create all the server threads
	for (servercore = 0; servercore < cores; servercore++) {
		
		cpu_set_t cpus;

		// Don't bother if we don't have a server on this core
		if ( ! sreq[servercore].bRunning )
			continue;

		// Set which CPU this thread should be on
		CPU_ZERO ( &cpus );
		CPU_SET ( servercore, &cpus );

		ret = pthread_create_on( &thread[threads], NULL, server_thread, &sreq[servercore], sizeof(cpus), &cpus);
		if ( ret ) {
			fprintf(stderr, "%s:%d pthread_create_on() error\n", __FILE__, __LINE__ );
			goto cleanup;
		}

		threads++;
	}

	// TODO REMOVE THIS SLEEP
	usleep( 100000 );

	for (clientcore = 0; clientcore < cores; clientcore++) {
		cpu_set_t cpus;

		if ( ! creq[clientcore].bRunning )
			continue;

		CPU_ZERO ( &cpus );
		CPU_SET ( clientcore, &cpus );

		ret = pthread_create_on( &thread[threads], NULL, client_thread, &creq [clientcore] , sizeof(cpus), &cpus);
		if ( ret ) {
			fprintf(stderr, "%s:%d pthread_create_on() error\n", __FILE__, __LINE__ );
			goto cleanup;
		}

		threads++;
	}

	// Spin lock until all the threads are ready
	// TODO change this to use a semaphore
	pthread_mutex_lock( &go_mutex );
	while ( bRunning && unready_threads > 0 ) {
		pthread_mutex_unlock( &go_mutex );
		usleep( 1000 );
		pthread_mutex_lock( &go_mutex );
	}
	pthread_cond_broadcast( &go_cond );
	pthread_mutex_unlock( &go_mutex );

	// Pauses for the duration, then sets bRunning to false
	pause_for_duration( &settings );

	// Block waiting until all threads die
	while (threads > 0) {
		threads--;
		pthread_join( thread[threads], NULL );
	}

	// Now sum all the results up
	i = 0;
	for (servercore = 0; servercore < cores; servercore++) {
		if ( sreq[ servercore ].port != 0 ) {
			total_stats.bytes_received += sreq[ servercore ].stats.bytes_received;
			total_stats.duration       += sreq[ servercore ].stats.duration;
			total_stats.pkts_received  += sreq[ servercore ].stats.pkts_received;
			total_stats.pkts_time  += sreq[ servercore ].stats.pkts_time;
			i++;
		}
	}

	// Divide the duration by the # of CPUs used
	total_stats.duration = total_stats.duration / i;

	
	print_results( &settings, -1, &total_stats );

cleanup:

	// Make sure we are not running anymore
	stop_all();

	// Block waiting until all threads die
	while (threads > 0) {
		threads--;
		pthread_join( thread[threads], NULL );
	}

	if ( clientserver ) {
		for (i = 0; i < cores; ++i)
			free ( clientserver[i] );

		free( clientserver );
	}

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
	}

	free( creq );
	free( sreq );

	pthread_cond_destroy( & go_cond );
	pthread_mutex_destroy( & go_mutex );

	free ( thread );

#ifdef WIN32
	cleanup_winsock();
#endif

	return 0;
}
