/***
	A network benchmark tool
	by Andrew Brampton (2007)

	Note, this app is very rough, and needs cleaning up, but it works!
	TODO Changed bRunning to be per request, rather than be global
	TODO Allow the app to work across networks
*/

#include "common.h"

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

// Settings

// Make a 2D array for each possible to and from connection
int **clientserver = NULL;

struct settings global_settings;

#ifdef WIN32
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

	// Make sure the thread is joinable
	ret = pthread_attr_setdetachstate( attr, PTHREAD_CREATE_JOINABLE);
	if (ret)
		goto cleanup;

	// Now create the thread
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
		fprintf(stderr, "%s:%d WSAStartup() error\n", __FILE__, __LINE__ );
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
	long long end_time; // The time we need to end

	// Make sure duration is in microseconds
	duration = duration * 1000000;

	// This main thread controls when the test ends
	end_time = get_microseconds() + duration;

	while ( bRunning ) {
		long long now = get_microseconds();

		if ( now > end_time ) {
			bRunning = 0;
			break;
		}

		if ( global_settings.verbose ) {
			printf(".");
			fflush(stdout);
		}

		// Pause for 0.1 second
		usleep( 100000 );
	}
	
	printf("\n");
}

void print_usage() {

	fprintf(stderr, "threadnetperf by bramp 2007\n" );
	fprintf(stderr, "Usage: threadnetperf [options] tests\n" );
	fprintf(stderr, "Runs a threaded network test\n" );

	fprintf(stderr, "\n" );

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

int parse_arguments( int argc, char *argv[] ) {
	int c;
	unsigned int x, y;

	// Default arguments
	global_settings.message_size = 1024;
	global_settings.socket_size = -1;
	global_settings.disable_nagles = 0;
	global_settings.duration = 10;
	global_settings.port = 1234;
	global_settings.verbose = 0;
	global_settings.dirty = 0;
	global_settings.timestamp = 0;

	global_settings.type = SOCK_STREAM;
	global_settings.protocol = IPPROTO_TCP;

	if ( argc == 1 ) {
		print_usage();
		return -1;
	}

	// Lets parse some command line args
	while ((c = getopt(argc, argv, "tTeunvhs:d:p:")) != -1) {
		switch ( c ) {
			// Duration
			case 'd':
				global_settings.duration = atoi( optarg );
				if ( global_settings.duration == 0 ) {
					fprintf(stderr, "Invalid duration given (%s)\n", optarg );
					return -1;
				}
				break;

			// Disable nagles algorithm (ie NO delay)
			case 'n':
				global_settings.disable_nagles = 1;
				break;

			// Parse the message size
			case 's':
				global_settings.message_size = atoi( optarg );
				if ( global_settings.message_size == 0 ) {
					fprintf(stderr, "Invalid message size given (%s)\n", optarg );
					return -1;
				}
				break;

			// Parse the message size
			case 'p':
				global_settings.port = atoi( optarg );
				if ( global_settings.port == 0 ) {
					fprintf(stderr, "Invalid port number given (%s)\n", optarg );
					return -1;
				}
				break;

			// Dirty the data
			case 'e':
				global_settings.dirty = 1;
				break;

			case 'T':
				global_settings.timestamp = 1;
				break;
			
			// Increase the verbose level
			case 'v':
				global_settings.verbose++;
				break;

			case 'h':
				print_usage();
				return -1;

			// TCP/UDP
			case 't':
				global_settings.type = SOCK_STREAM;
				global_settings.protocol = IPPROTO_TCP;
				break;

			case 'u':
				global_settings.type = SOCK_DGRAM;
				global_settings.protocol = IPPROTO_UDP;
				break;

			case '?':
				fprintf(stderr, "Unknown argument (%s)\n", argv[optind-1] );
				return -1;
			default:
				fprintf(stderr, "Argument not implemented (yet) (%c)\n", c );
				return -1;
		}
	}

	if ( global_settings.disable_nagles && global_settings.protocol != IPPROTO_TCP ) {
		fprintf(stderr, "Must use TCP when disabling Nagles\n" );
		return -1;
	}
	
	if( global_settings.timestamp && global_settings.message_size < sizeof(unsigned long long) ) {
		fprintf(stderr, "Message size must be greater than %u when using timestamps\n",  sizeof(unsigned long long) );
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

		optind++;
	}

	return 0;
}

void print_headers() {
	
	printf("Core\tsend\treceived\tnum\ttime\tgoodput%s\n",
		global_settings.timestamp ? "\tpacket" : "");

	printf("\tmsg\tbytes\t\trecv()s\t\t(MB/s)\t%s\n",
		global_settings.timestamp ? "\tlatency" : "");

	printf("\tsize\t\t\t\t\t\t\n");
}

void print_results( int core, struct stats *stats ) {
	float thruput = stats->bytes_received > 0 ? (float)stats->bytes_received / (float)stats->duration : 0;
	float duration = (float)stats->duration / (float)1000000;
//	float pkt_latency = (float)stats->pkts_time /  (float)stats->pkts_received;

#ifdef WIN32 // Work around a silly windows bug in handling %llu
	printf( "%i\t%u\t%I64u\t%I64u\t%.2fs\t%.2f", 
#else
	printf( "%i\t%u\t%llu\t%llu\t%.2fs\t%.2f",
#endif
		core, global_settings.message_size, stats->bytes_received, stats->pkts_received, duration, thruput );

	if ( global_settings.timestamp )
		printf( "\t%lluus",stats->pkts_time );

	printf("\n");
	
#ifdef CHECK_TIMES 
{ 	
	int i;
	if(stats->processed_something) {
		for(i=0; i<CHECK_TIMES && i < stats-> pkts_received; i++ ) {
	
			printf("%f\n", stats->processing_times[i]);
		} 
	}
}
	printf("\n");
#endif
}

int main (int argc, char *argv[]) {
	struct server_request *sreq = NULL;
	struct client_request **creq = NULL;

	pthread_t *thread = NULL; // Array to handle thread handles
	unsigned int threads = 0; // Total number of threads
	unsigned int i;
	int ret;
	unsigned int servercore, clientcore;

	// The sum of all the stats
	struct stats total_stats = {0,0,0};

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


	if ( parse_arguments( argc, argv ) ) {
		goto cleanup;
	}
	print_headers();
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

			struct client_request *c;

			// Don't bother if there are zero requests
			if ( clientserver [ clientcore ] [ servercore ] == 0 )
				continue;

			// Check if we haven't set up this server yet
			if ( sreq [ servercore ].port == 0 ) {
				sreq [ servercore ].port = global_settings.port + servercore;
				sreq [ servercore ].stats.duration = global_settings.duration;
				sreq [ servercore ].n = 0;
				sreq [ servercore ].core = servercore;
				unready_threads++;
			}
			
			// Now create the client
			c = creq [ clientcore ];

			// If this is the first then add a thread
			if ( c == NULL ) {
				unready_threads++;
				c = malloc( sizeof(*c) );
				creq [ clientcore ] = c;
			} else {

				// Find the last 
				while ( c->next != NULL )
					c = c->next;

				c->next = malloc( sizeof( *c ) );
				c = c->next;
			}

			memset(c, 0, sizeof(*c));

			c->core = clientcore;
			c->n = clientserver [ clientcore ] [ servercore ];
			sreq [ servercore ].n += c->n;

			// Create the client dest addr
			c->addr_len = sizeof ( struct sockaddr_in );

			c->addr = malloc ( c->addr_len ) ;
			memset( c->addr, 0, c->addr_len );

			((struct sockaddr_in *)c->addr)->sin_family = AF_INET;
			((struct sockaddr_in *)c->addr)->sin_addr.s_addr = inet_addr( "127.0.0.1" );
			((struct sockaddr_in *)c->addr)->sin_port = htons( global_settings.port + servercore );

		}
	}

	// If there are no paramters then error
	if ( unready_threads == 0 ) {
		fprintf(stderr, "Please enter atleast one client/server combination\n");
		goto cleanup;
	}

	// A list of threads
	thread = malloc( unready_threads * sizeof(*thread) );
	memset (thread, 0, unready_threads * sizeof(*thread));

	// Create all the server threads
	for (servercore = 0; servercore < cores; servercore++) {
		
		cpu_set_t cpus;

		// Don't bother if we don't have a server on this core
		if ( sreq[servercore].port == 0)
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

		if ( creq[clientcore] == NULL)
			continue;

		CPU_ZERO ( &cpus );
		CPU_SET ( clientcore, &cpus );

		ret = pthread_create_on( &thread[threads], NULL, client_thread, creq [clientcore] , sizeof(cpus), &cpus);
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
	pause_for_duration( global_settings.duration );

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

	
	print_results( -1, &total_stats );

cleanup:

	// Make sure we are not running anymore
	bRunning = 0;

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
			struct client_request *c = creq[i];
			while ( c != NULL ) {
				struct client_request *nextC = c->next;
				free ( c->addr );
				free( c );
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
