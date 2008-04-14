#include "client.h"
#include "global.h"

#include <assert.h>
#include <malloc.h>
#include <stdio.h>

// Array of all the client requests
struct client_request *creq = NULL;
size_t creq_size = 0;

int prepare_clients(const struct settings * settings, void *data) {

	unsigned int clientthreads = 0;
	unsigned int i;

	assert ( settings != NULL );
	assert ( settings->test != NULL );
	assert ( creq == NULL );
	assert ( creq_size == 0 );

	// Malloc one space for each core combination
	creq_size = settings->servercores;

	// Malloc one space for each core
	creq = calloc ( creq_size, sizeof(*creq) );
	if ( !creq ) {
		creq_size = 0;
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		return -1;
	}

	// Loop through clientserver looking for each server we need to create
	for ( i = 0; i < settings->tests; i++) {
		const struct test * test = &settings->test[i];
		struct client_request *c;
		struct client_request_details *details;

		// find an exisiting sreq with this core combo
		for ( c = creq; c < &creq[creq_size]; c++) {
			if ( c->bRunning == 0 || c->core == test->clientcores )
				break;
		}
		assert ( c < &creq[creq_size] );

		// Check if we haven't set up this client thread yet
		if ( c->bRunning == 0 ) {
			c->bRunning = 1;
			c->settings = settings;
			c->core = test->clientcores;

			unready_threads++;
			clientthreads++;
		}

		// Malloc the request details
		details = calloc( 1, sizeof( *details ) );
		if ( !details ) {
			fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
			return -1;
		}

		// Add this new details before the other details
		details->next =c->details;
		c->details = details;

		details->n = test->connections;

		// Create the client dest addr
		details->addr_len = sizeof ( struct sockaddr_in );

		details->addr = calloc ( 1, details->addr_len ) ;
		if ( !details->addr ) {
			fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
			return -1;
		}

		// Change this to be more address indepentant
		((struct sockaddr_in *)details->addr)->sin_family = AF_INET;
		((struct sockaddr_in *)details->addr)->sin_addr.s_addr = inet_addr( settings->server_host );
		((struct sockaddr_in *)details->addr)->sin_port = htons( settings->port + 0 ); // TODO PORT

		if ( ((struct sockaddr_in *)details->addr)->sin_addr.s_addr == INADDR_NONE ) {
			fprintf(stderr, "Invalid host name (%s)\n", settings->server_host );
			return -1;
		}
	}

	// Double check we made the correct number of servers
	assert ( creq[creq_size - 1].bRunning == 1 );

	return 0;
}

int create_clients(const struct settings *settings, void *data) {
	unsigned int i;

	assert ( settings != NULL );
	assert ( creq != NULL );

	for (i = 0; i < creq_size; i++) {
		cpu_set_t cpus;

		assert ( creq[i].bRunning );

		CPU_ZERO ( &cpus );
		CPU_SET ( creq[i].core , &cpus );

		if ( create_thread( client_thread, &creq [i] , sizeof(cpus), &cpus) ) {
			fprintf(stderr, "%s:%d create_thread() error\n", __FILE__, __LINE__ );
			return -1;
		}
	}

	return 0;
}

void stop_all_clients() {
	if ( creq ) {
		unsigned int i = 0;

		assert ( creq_size != 0 );

		for (; i < creq_size; i++) {
			creq[i].bRunning = 0;
		}
	}
}

void cleanup_clients() {
	unsigned int i;

	if ( creq ) {
		assert ( creq_size != 0 );

		for (i = 0; i < creq_size; i++) {

			// Free the chain of details
			struct client_request_details *c = creq[i].details;
			while ( c != NULL ) {
				struct client_request_details *nextC = c->next;
				free ( c->addr );
				free ( c );
				c = nextC;
			}
		}

		free( creq );
		creq = NULL;
		creq_size = 0;
	}
}
