#include "serialise.h"

#include <assert.h>
#include <stdio.h>
#include <malloc.h>

// Reads settings from a socket
int read_settings( SOCKET s, struct settings * settings ) {

	int ret;
	int x;

	assert ( s != INVALID_SOCKET );
	assert ( settings != NULL );

	ret = recv(s, (char *)settings, sizeof(*settings), 0);
	if ( ret != sizeof(*settings) || settings->version != SETTINGS_VERSION ) {

		if ( ret > 0 )
			fprintf(stderr, "Invalid setting struct received\n" );

		return -1;
	}

	// Now construct the clientserver table
	settings->clientserver = (int **)malloc_2D(sizeof(int), settings->cores, settings->cores);

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
	assert ( s != INVALID_SOCKET );
	assert ( settings != NULL );

	return -1;
}