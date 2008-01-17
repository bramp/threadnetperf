#include "serialise.h"

#include <assert.h>
#include <stdio.h>
#include <malloc.h>

#ifdef WIN32
	typedef signed char             int8_t;
	typedef unsigned char           uint8_t;
	typedef signed int              int16_t;
	typedef unsigned int            uint16_t;
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
	#define SETTINGS_VERSION 3 // Increment this each time the setting struct changes

	uint32_t duration;
	
	uint32_t type;
	uint32_t protocol;
	
	uint8_t verbose;
	uint8_t dirty;
	uint8_t timestamp;
	uint8_t disable_nagles;

	uint32_t message_size;	
	uint32_t socket_size;

	uint16_t port;

	// A 2D array for each possible to and from core (with number of connections)
	uint32_t cores;
};

// Reads settings from a socket
int read_settings( SOCKET s, struct settings * settings ) {
	int ret;
	int x;
	struct network_settings net_settings;

	assert ( s != INVALID_SOCKET );
	assert ( settings != NULL );

	ret = recv(s, (char *)&net_settings, sizeof(net_settings), 0);
	if ( ret != sizeof(net_settings) || net_settings.version != SETTINGS_VERSION ) {
		if ( ret > 0 )
			fprintf(stderr, "Invalid setting struct received\n" );

		return -1;
	}

	// Set all the fields
	settings->duration       = net_settings.duration;
	settings->type           = net_settings.type;
	settings->protocol       = net_settings.protocol;
	
	settings->verbose        = net_settings.verbose;
	settings->dirty          = net_settings.dirty;
	settings->timestamp      = net_settings.timestamp;
	settings->disable_nagles = net_settings.disable_nagles;

	settings->message_size   = net_settings.message_size;
	settings->socket_size    = net_settings.socket_size;

	settings->port           = net_settings.port;
	settings->cores          = net_settings.cores;
	
	// Blank some fields
	settings->deamon         = 0;
	settings->confidence_lvl = 0.0;
	settings->min_iterations = 1;
	settings->max_iterations = 1;

	settings->server_host    = NULL;

	// Now construct the clientserver table
	settings->clientserver = (int **)malloc_2D(sizeof(int), settings->cores, settings->cores);
	if ( settings->clientserver == NULL ) {
		fprintf(stderr, "%s:%d malloc_2D() error\n", __FILE__, __LINE__);
		return -1;
	}

	for (x = 0; x < settings->cores; x++) {
		int *row = settings->clientserver[x];
		ret = recv(s, (char *)row, sizeof(*row) * settings->cores, 0);
		if ( ret != sizeof(*row) * settings->cores ) {
			return -1;
		}
	}

	return 0;
}

// Sends settings to a socket
int send_settings( SOCKET s, const struct settings * settings ) {
	int ret;
	int x;
	struct network_settings net_settings;

	assert ( s != INVALID_SOCKET );
	assert ( settings != NULL );

	// Copy all the settings into a struct which can be sent over the network easily
	net_settings.version        = SETTINGS_VERSION;
	net_settings.duration       = settings->duration;
	net_settings.type           = settings->type;
	net_settings.protocol       = settings->protocol;
	
	net_settings.verbose        = settings->verbose;
	net_settings.dirty          = settings->dirty;
	net_settings.timestamp      = settings->timestamp;
	net_settings.disable_nagles = settings->disable_nagles;

	net_settings.message_size   = settings->message_size;
	net_settings.socket_size    = settings->socket_size;

	net_settings.port           = settings->port;
	net_settings.cores          = settings->cores;

	ret = send(s, (char *)&net_settings, sizeof(net_settings), 0);
	if ( ret != sizeof(net_settings) ) {
		return -1;
	}

	for (x = 0; x < settings->cores; x++) {
		int *row = settings->clientserver[x];
		ret = send(s, (char *)row, sizeof(*row) * settings->cores, 0);
		if ( ret != sizeof(*row) * settings->cores ) {
			return -1;
		}
	}

	return -1;
}
