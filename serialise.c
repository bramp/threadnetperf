#include "serialise.h"

#include <assert.h>
#include <stdio.h>
#include <malloc.h>

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
	unsigned int version;
	#define SETTINGS_VERSION 4 // Increment this each time the setting struct changes

	uint32_t duration;
	
	uint32_t type;
	uint32_t protocol;
	
	uint8_t verbose;
	uint8_t dirty;
	uint8_t timestamp;
	uint8_t disable_nagles;

	uint32_t message_size;	
	uint32_t socket_size;

	uint32_t cores;
	uint16_t port;
};

// Reads settings from a socket
int read_settings( SOCKET s, struct settings * settings ) {
	int ret;
	unsigned int x, y;
	struct network_settings net_settings;
	uint32_t *buffer = NULL;
	unsigned int buffer_len = 0;

	assert ( s != INVALID_SOCKET );
	assert ( settings != NULL );

	ret = recv(s, (char *)&net_settings, sizeof(net_settings), 0);
	if ( ret != sizeof(net_settings) || net_settings.version != ntohl(SETTINGS_VERSION) ) {
		if ( ret > 0 )
			fprintf(stderr, "Invalid setting struct received\n" );

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

	settings->cores          = ntohl( net_settings.cores );
	settings->port           = ntohs( net_settings.port );
	
	// Blank some fields
	settings->deamon         = 0;
	settings->confidence_lvl = 0.0;
	settings->confidence_int = 0.0;
	settings->min_iterations = 1;
	settings->max_iterations = 1;

	settings->server_host    = NULL;
	settings->clientserver   = NULL;

	// Create a buffer to read all the values
	buffer_len = settings->cores * settings->cores * sizeof( uint32_t );
	buffer = malloc( buffer_len );
	if ( buffer == NULL ) {
		fprintf(stderr, "%s:%d malloc() error\n", __FILE__, __LINE__);
		return -1;
	}

	ret = recv(s, (char *)buffer, buffer_len, 0);
	if ( ret != buffer_len ) {
		return -1;
	}

	// Now construct the clientserver table
	settings->clientserver = (unsigned int **)malloc_2D(sizeof(unsigned int), settings->cores, settings->cores);
	if ( settings->clientserver == NULL ) {
		fprintf(stderr, "%s:%d malloc_2D() error\n", __FILE__, __LINE__);
		free(buffer);
		return -1;
	}

	for (x = 0; x < settings->cores; x++) {
		for (y=0; y < settings->cores; y++) {
			settings->clientserver[x][y] = ntohl( buffer [ x * settings->cores + y ] );
		}
	}

	free(buffer);

	return 0;
}

// Sends settings to a socket
int send_settings( SOCKET s, const struct settings * settings ) {
	int ret;
	unsigned int x, y;
	struct network_settings net_settings;
	uint32_t *buffer = NULL;
	unsigned int buffer_len = 0;

	assert ( s != INVALID_SOCKET );
	assert ( settings != NULL );

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

	net_settings.cores          = htonl( settings->cores );
	net_settings.port           = htons( settings->port );

	ret = send(s, (char *)&net_settings, sizeof(net_settings), 0);
	if ( ret != sizeof(net_settings) ) {
		return -1;
	}

	// Build a buffer with all the client/server combinations
	buffer_len = settings->cores * settings->cores * sizeof( uint32_t );
	buffer = malloc( buffer_len );
	if ( buffer == NULL )
		return -1;

	for (x = 0; x < settings->cores; x++) {
		for (y=0; y < settings->cores; y++) {
			buffer [ x * settings->cores + y ] = htonl( settings->clientserver[x][y] );
		}
	}

	ret = send(s, (const char *)buffer, buffer_len, 0);
	free (buffer);

	if ( ret != buffer_len ) {
		return -1;
	}

	return 0;
}
