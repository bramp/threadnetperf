#include "server.h"
#include "global.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef WIN32
#include <unistd.h> //usleep
#endif

// Array of all the server requests
struct server_request *sreq = NULL;
size_t sreq_size = 0;

int prepare_servers(const struct settings * settings, void *data) {
	unsigned int i;
	int serverthreads = 0;

	assert ( settings != NULL );
	assert ( settings->test != NULL );

	assert ( sreq == NULL );
	assert ( sreq_size == 0 );

	// Malloc one space for each core combination
	sreq_size = settings->servercores;

	sreq = calloc (sreq_size , sizeof(*sreq) );
	if ( !sreq ) {
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		sreq_size = 0;
		return -1;
	}

	// Now setup the server structs for each core combination
	for ( i = 0; i < settings->tests; i++) {
		struct server_request *s;
		const struct test * test = &settings->test[i];

		// find an exisiting sreq with this core combo
		for ( s = sreq; s < &sreq[sreq_size]; s++) {
			if ( s->settings == NULL || s->cores == test->servercores )
				break;
		}
		assert ( s < &sreq[sreq_size] );

		// Check if we haven't set up this server thread yet
		if ( s->settings == NULL ) {
			s->settings = settings;
			s->cores = test->servercores;

			// TODO change this from a simple port to a full sockaddr struct
			s->port = ntohs( ((struct sockaddr_in *)&test->addr)->sin_port );
			s->n = 0;

			serverthreads++;
		}

		s->n += test->connections;
	}

	// Double check we made the correct number of servers
	assert ( sreq[sreq_size - 1].settings != NULL );

	return serverthreads;
}

int create_servers(const struct settings *settings, void *data) {
	unsigned int i;

	assert ( sreq_size == settings->servercores );

	// Create all the server threads
	for (i = 0; i < sreq_size; i++) {

		cpu_set_t cpus;

		assert ( sreq[i].settings != NULL );

		// Set which CPU this thread should be on
		cpu_setup( &cpus, sreq[i].cores );

		if ( create_thread( server_thread, &sreq[i] , sizeof(cpus), &cpus, settings->threaded_model) ) {
			fprintf(stderr, "%s:%d create_thread() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO));
			return -1;
		}
	}

	return 0;
}

void cleanup_servers() {
	free( sreq );
	sreq = NULL;
	sreq_size = 0;
}
