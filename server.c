#include "server.h"
#include "global.h"

#include <assert.h>
#include <malloc.h>
#include <stdio.h>

#ifndef WIN32
#include <unistd.h> //usleep
#endif

// Array of all the server requests
struct server_request *sreq = NULL;

int prepare_servers(const struct settings * settings) {

	unsigned int servercore, clientcore;
	unsigned int ** clientserver;

	assert ( settings != NULL );
	assert ( sreq == NULL );
	
	// Malloc one space for each core
	sreq = calloc ( settings->cores, sizeof(*sreq) );
	if ( !sreq ) {
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		return -1;
	}

	clientserver = settings->clientserver;
	assert ( clientserver != NULL );

	// Loop through clientserver looking for each server we need to create
	for (servercore = 0; servercore < settings->cores; servercore++) {
		for (clientcore = 0; clientcore < settings->cores; clientcore++) {

			// Don't bother if there are zero requests
			if ( clientserver [ clientcore ] [ servercore ] == 0 )
				continue;

			if ( clientcore > max_cores || servercore > max_cores ) {
				fprintf(stderr, "Too many cores! %u > %u\n", clientcore > servercore ? clientcore : servercore, max_cores );
				return -1;
			}

			// Check if we haven't set up this server thread yet
			if ( sreq [ servercore ].bRunning == 0 ) {
				sreq [ servercore ].bRunning = 1;
				sreq [ servercore ].settings = settings;
				sreq [ servercore ].port = settings->port + servercore;
				sreq [ servercore ].n = 0;
				sreq [ servercore ].core = servercore;

				unready_threads++;
				server_listen_unready++;
			}

			sreq [ servercore ].n += clientserver [ clientcore ] [ servercore ];
		}
	}


	return 0;
}

int create_servers(const struct settings *settings) {
	unsigned int servercore;

	// Create all the server threads
	for (servercore = 0; servercore < settings->cores; servercore++) {
		
		cpu_set_t cpus;

		// Don't bother if we don't have a server on this core
		if ( ! sreq[servercore].bRunning )
			continue;

		// Set which CPU this thread should be on
		CPU_ZERO ( &cpus );
		CPU_SET ( servercore, &cpus );

		if ( pthread_create_on( &thread[threads], NULL, server_thread, &sreq[servercore], sizeof(cpus), &cpus) ) {
			fprintf(stderr, "%s:%d pthread_create_on() error\n", __FILE__, __LINE__ );
			return -1;
		}

		threads++;
	}

	// TODO make this not a spin lock
	// Wait until all the servers are ready to accept connections
	while ( bRunning && server_listen_unready > 0 ) {
		usleep( 1000 );
	}

	return 0;
}

void stop_all_servers(const struct settings *settings) {
	if ( sreq ) {
		unsigned int i = 0;
		for (; i < settings->cores; i++) {
			sreq[i].bRunning = 0;
		}
	}
}

void cleanup_servers() {
	free( sreq );
	sreq = NULL;
}
