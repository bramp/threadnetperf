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
size_t sreq_size = 0;

int prepare_servers(const struct settings * settings, void *data) {

	unsigned int i;

	assert ( settings != NULL );
	assert ( settings->test != NULL );

	assert ( sreq == NULL );
	assert ( sreq_size == 0 );

	printf("Need to create %d servers\n", settings->servercores);
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
			if ( s->bRunning == 0 || s->cores == test->servercores )
				break;
		}
		assert ( s < &sreq[sreq_size] );

		// Check if we haven't set up this server thread yet
		if ( s->bRunning == 0 ) {
			s->bRunning = 1;
			
			s->settings = settings;
			s->cores = test->servercores;

			// TODO change this from a simple port to a full sockaddr struct
			s->port = ntohs( ((struct sockaddr_in *)&test->addr)->sin_port );
			s->n = 0;
			
			unready_threads++;
			server_listen_unready++;
		}

		s->n += test->connections;
	}

	// Double check we made the correct number of servers
	assert ( sreq[sreq_size - 1].bRunning == 1 );

	return 0;
}

int create_servers(const struct settings *settings, void *data) {
	unsigned int i;

	assert ( sreq_size == settings->servercores );

	// Create all the server threads
	for (i = 0; i < sreq_size; i++) {

		cpu_set_t cpus;
	
		// Don't bother if we don't have a server on this core
		assert ( sreq[i].bRunning );

		// Set which CPU this thread should be on
		cpu_setup( &cpus, sreq[i].cores );
		

		if ( create_thread( server_thread, &sreq[i] , sizeof(cpus), &cpus, settings->threaded_model) ) {
			fprintf(stderr, "%s:%d create_thread() error\n", __FILE__, __LINE__ );
			return -1;
		}
	}

	// TODO make this not a spin lock
	// Wait until all the servers are ready to accept connections
	while ( bRunning && server_listen_unready > 0 ) {
		usleep( 1000 );
	}

	return 0;
}

void stop_all_servers() {
	if ( sreq ) {
		unsigned int i = 0;

		assert ( sreq_size != 0 );

		for (; i < sreq_size; i++) {
			sreq[i].bRunning = 0;
		}
	}
}

void cleanup_servers() {
	free( sreq );
	sreq = NULL;
	sreq_size = 0;
}
