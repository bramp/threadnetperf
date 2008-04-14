#include "parse.h"
#include "common.h"
#include "version.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
	#include "getopt.h"
#else
	#include <unistd.h>
#endif

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
	fprintf(stderr, "	tests      Core numbers are masks, for example 1 is core 0, 3 is core 0 and core 1\n" );
	fprintf(stderr, "		N{c-s}   N connections\n" );
	fprintf(stderr, "		         c client cores mask\n" );
	fprintf(stderr, "		         s server cores mask\n" );


	fprintf(stderr, "\n" );
	fprintf(stderr, "Examples:\n" );
	fprintf(stderr, "	> threadnetperf -n -s 10000 1{1-1}\n" );
	fprintf(stderr, "	Disable Nagle's, send size of 10000 with 1 connection from core 0 to core 0\n" );

	fprintf(stderr, "\n" );
	fprintf(stderr, "	> threadnetperf 10{1-1} 10{2-2} 10{4-4}\n" );
	fprintf(stderr, "	10 connection from core 0 to core 0, 10 connections from core 1 to core 1, and 10 connections from core 2 to core 2\n" );

}

// Parses N(C-S)
int parse_test( const struct settings *settings, const char *arg, struct test * test ) {

	char hostname[NI_MAXHOST + NI_MAXSERV + 1];

	assert( arg != NULL );
	assert( test != NULL );

	if ( sscanf( arg, "%u(%u-%u:%1000s)", &test->connections, &test->clientcores, &test->servercores, hostname ) == 4 ) {
		// Find the last ) and remove
		char *c = strchr(hostname, ')');
		if ( c != NULL )
			*c = '\0';
		goto good;
	}

	// Parse with different brackets
	if ( sscanf( arg, "%u{%u-%u:%1000s}", &test->connections, &test->clientcores, &test->servercores, hostname ) == 4 ) {
		// Find the last ) and remove
		char *c = strchr(hostname, '}');
		if ( c != NULL )
			*c = '\0';
		goto good;
	}

	if ( settings->server_host != NULL )
		strncpy(hostname, settings->server_host, sizeof(hostname));

	if ( sscanf( arg, "%u(%u-%u)", &test->connections, &test->clientcores, &test->servercores ) == 3 ) {
		goto good;
	}

	// Parse with different brackets
	if ( sscanf( arg, "%u{%u-%u}", &test->connections, &test->clientcores, &test->servercores ) == 3 ) {
		goto good;
	}

	return -1;

good:

	memset( &test->addr, 0, sizeof(test->addr) );

	if ( hostname[0] != '\0' ) {
		test->addr_len = sizeof(struct sockaddr_in);
		str_to_addr( hostname, (struct sockaddr *) &test->addr, &test->addr_len );
		((struct sockaddr_in *)&test->addr)->sin_port = htons( settings->port + test->servercores );

		if ( ((struct sockaddr_in *)&test->addr)->sin_addr.s_addr == INADDR_NONE ) {
			fprintf(stderr, "Invalid host name (%s)\n", hostname );
			return -1;
		}
	}

	return 0;
}

int parse_settings( int argc, char *argv[], struct settings *settings ) {
	int c;
	const char *optstring = "DhvVtTeuns:d:p:c:i:H:r:";

	assert ( settings != NULL );

	// Default arguments
	settings->deamon = 0;
	settings->message_size = 1024;
	settings->socket_size = ~0;
	settings->rate = ~0;
	settings->disable_nagles = 0;
	settings->duration = 10;
	settings->server_host = NULL;
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

	settings->test = malloc(0);
	settings->tests = 0;

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

		struct test * test;

		// Malloc space for this extra test
		settings->test = realloc ( settings->test, sizeof(*settings->test) * (settings->tests + 1) );
		test = &settings->test [ settings->tests ];

		// Parse N{C-S}
		if ( parse_test ( settings, argv[optind], test ) != 0 ) {
			fprintf(stderr, "Unknown argument (%s)\n", argv[optind] );
			return -1;
		}

		// Check all the paramters make sense
		if ( test->clientcores == 0 || test->servercores == 0 ) {
			fprintf(stderr, "Cores of zero will not run on any core (%s)\n", argv[optind] );
			return -1;
		}

		// TODO check if the server is remote, and then decide if the cores make sense
		if ( test->clientcores >= (unsigned int)(1 << max_cores) || test->servercores >= (unsigned int)(1 << max_cores) ) {
			fprintf(stderr, "Cores must not be greater than %d (%s)\n", max_cores, argv[optind] );
			return -1;
		}

		settings->tests++;
		optind++;
	}

	// If there are no tests then error
	if ( settings->tests == 0 && !settings->deamon ) {
		fprintf(stderr, "No tests were specified\n");
		return -1;
	}

	settings->clientcores = count_client_cores ( settings->test, settings->tests );
	settings->servercores = count_server_cores ( settings->test, settings->tests );

	return 0;
}

