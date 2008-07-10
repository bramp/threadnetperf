#include "serialise.h"

#include "parse.h"

#include <assert.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

#ifdef WIN32
	typedef signed char             int8_t;
	typedef unsigned char           uint8_t;
	typedef signed short            int16_t;
	typedef unsigned short          uint16_t;
	typedef signed long int         int32_t;
	typedef unsigned long int       uint32_t;
	typedef signed long long int    int64_t;
	typedef unsigned long long int  uint64_t;
#else
#include <stdint.h>
#endif

// A version of the settings struct which can be sent over the network
struct network_settings {

	// Version of the setting struct, this must be the first element
	uint32_t version;
	#define SETTINGS_VERSION 6 // Increment this each time the setting struct changes

	uint32_t duration;

	uint32_t type;
	uint32_t protocol;

	// TODO collapse these boolean values into one byte
	uint8_t verbose;
	uint8_t dirty;
	uint8_t timestamp;
	uint8_t disable_nagles;
	
	
	uint32_t message_size;
	uint32_t socket_size;

	uint32_t rate; // Sends per second

	uint16_t port;

	uint8_t tests; // The number of tests
};

struct network_stats {
	// The cores these stats were recorded from
	uint32_t cores;

	// The number of bytes received
	uint64_t bytes_received;

	// The number of recv() handled
	uint64_t pkts_received;

	// The duration packets were inside the network
	uint64_t pkts_time;

	//Total number of timestamp recorded
	uint64_t timestamps;

	// The duration over which these stats were recorded
	uint64_t duration;
};

// Reads settings from a socket
// TODO add extra check
int read_settings( SOCKET s, struct settings * settings ) {
	int ret;
	unsigned int i = 0;

	struct network_settings net_settings;

	assert ( s != INVALID_SOCKET );
	assert ( settings != NULL );

	memset( &net_settings, 0, sizeof(net_settings) );

	ret = recv(s, (char *)&net_settings, sizeof(net_settings), 0);
	if ( ret != sizeof(net_settings) || net_settings.version != ntohl(SETTINGS_VERSION) ) {
		if ( ret > 0 )
			fprintf(stderr, "Invalid setting struct received (size:%d vs %d, ver:%d vs %d\n", ret, (int)sizeof(net_settings), net_settings.version, ntohl(SETTINGS_VERSION) );

		return -1;
	}

	// Set all the fields
	settings->duration       = ntohl( net_settings.duration );
	settings->type           = ntohl( net_settings.type );
	settings->protocol       = ntohl( net_settings.protocol );

	settings->verbose        = net_settings.verbose;
	settings->dirty          = net_settings.dirty;
	settings->timestamp      = net_settings.timestamp;
	settings->disable_nagles = net_settings.disable_nagles;

	settings->message_size   = ntohl( net_settings.message_size );
	settings->socket_size    = ntohl( net_settings.socket_size );

	settings->rate			 = ntohl( net_settings.rate );

	settings->port           = ntohs( net_settings.port );

	settings->tests          = net_settings.tests;

	// Blank some fields
	settings->deamon         = 0;
	settings->confidence_lvl = 0.0;
	settings->confidence_int = 0.0;
	settings->min_iterations = 1;
	settings->max_iterations = 1;
	settings->server_host    = NULL;

	// Create space for all the tests
	settings->test = calloc( settings->tests, sizeof(*settings->test) );
	if ( !settings->test ) {
		fprintf(stderr, "%s:%d read_settings() calloc error\n", __FILE__, __LINE__ );
		return -1;
	}

	// Now parse each test, one at a time
	for (i = 0; i < settings->tests; i++) {
		char buffer[ 256 ];
		unsigned char buflen;

		ret = recv(s, &buflen, sizeof(buflen), 0);
		if ( ret != sizeof(buflen) ) {
			return -1;
		}

		ret = recv(s, buffer, buflen, 0);
		if ( ret != buflen ) {
			return -1;
		}
		buffer [ buflen ] = '\0';

		if ( parse_test(settings, buffer, &settings->test[i]) )
			return -1;
	}

	settings->servercores  = count_server_cores( settings->test, settings->tests) ;
	settings->clientcores  = count_client_cores( settings->test, settings->tests) ;

	return 0;
}

// Sends settings to a socket
int send_settings( SOCKET s, const struct settings * settings ) {
	int ret;
	struct network_settings net_settings;
	unsigned int i;

	assert ( s != INVALID_SOCKET );
	assert ( settings != NULL );

	memset( &net_settings, 0, sizeof(net_settings) );

	// Copy all the settings into a struct which can be sent over the network easily
	net_settings.version        = htonl( SETTINGS_VERSION );
	net_settings.duration       = htonl( settings->duration );
	net_settings.type           = htonl( (unsigned int)settings->type );
	net_settings.protocol       = htonl( (unsigned int)settings->protocol );

	net_settings.verbose        = settings->verbose;
	net_settings.dirty          = settings->dirty;
	net_settings.timestamp      = settings->timestamp;
	net_settings.disable_nagles = settings->disable_nagles;

	net_settings.message_size   = htonl( settings->message_size );
	net_settings.socket_size    = htonl( settings->socket_size );
	net_settings.rate           = htonl( settings->rate );

	net_settings.port           = htons( settings->port );

	net_settings.tests          = settings->tests;

	ret = send(s, (char *)&net_settings, sizeof(net_settings), 0);
	if ( ret != sizeof(net_settings) ) {
		return -1;
	}

	for (i = 0; i < settings->tests; i++) {
		char buffer[256 + 1];
		const struct test * test = &settings->test[i];
		sprintf(&buffer[1], "%u(%u-%u) ", test->connections, test->clientcores, test->servercores);

		assert ( strlen(&buffer[1]) < 256 );
		buffer[0] = (char)strlen(&buffer[1]);

		ret = send(s, buffer, *buffer + 1, 0);
		if ( ret != *buffer + 1 ) {
			return -1;
		}
	}

	return 0;
}

int read_results( SOCKET s, struct stats * stats ) {
	struct network_stats net_stats;

	char *p = &net_stats;
	size_t p_len = sizeof(net_stats);

	int ret;

	assert ( s != INVALID_SOCKET );
	assert ( stats != NULL );

	memset( &net_stats, 0, sizeof(net_stats) );

	// Keep looping until the full net_stat struct is read
	do {
		ret = recv(s, p, p_len, 0);
		if ( ret <= 0 )
			return -1;

		p_len -= ret;
		p += ret;
	} while ( p_len != 0 );

	// TODO find a 64bit ntohl
	stats->cores          = ntohl(net_stats.cores);
	stats->bytes_received = (net_stats.bytes_received);
	stats->pkts_received  = (net_stats.pkts_received);
	stats->pkts_time      = (net_stats.pkts_time);
	stats->timestamps     = (net_stats.timestamps);
	stats->duration       = (net_stats.duration);

	return 0;
}

int send_results( SOCKET s, const struct stats * stats ) {
	struct network_stats net_stats;
	int ret;

	assert (s != INVALID_SOCKET);
	assert (stats != NULL );

	memset( &net_stats, 0, sizeof(net_stats) );

	// TODO find a 64bit htonl
	net_stats.cores          = htonl(stats->cores);
	net_stats.bytes_received = (stats->bytes_received);
	net_stats.pkts_received  = (stats->pkts_received);
	net_stats.pkts_time	     = (stats->pkts_time);
	net_stats.timestamps     = (stats->timestamps);
	net_stats.duration       = (stats->duration);

	ret = send(s, (char *)&net_stats, sizeof(net_stats), 0);
	if ( ret != sizeof(net_stats) ) {
		return -1;
	}

	return 0;
}
