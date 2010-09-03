#include "client.h"
#include "global.h"

#include <assert.h>
#include <stdlib.h>
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
	creq_size = settings->clientcores;

	// Malloc one space for each core
	creq = calloc ( creq_size, sizeof(*creq) );
	if ( !creq ) {
		creq_size = 0;
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		return -1;
	}

	// Loop through clientserver looking for each client we need to create
	for ( i = 0; i < settings->tests; i++) {
		const struct test * test = &settings->test[i];
		struct client_request *c;
		struct client_request_details *details;

		// find an exisiting creq with this core combo
		for ( c = creq; c < &creq[creq_size]; c++) {
			if ( c->settings == NULL || c->cores == test->clientcores )
				break;
		}
		assert ( c < &creq[creq_size] );

		// Check if we haven't set up this client thread yet
		if ( c->settings == NULL ) {
			c->settings = settings;
			c->cores = test->clientcores;

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

		details->n        = test->connections;
		details->addr     = test->addr;
		details->addr_len = test->addr_len;
	}

	// Double check we made the correct number of clients
	assert ( creq[creq_size - 1].settings != NULL );

	return clientthreads;
}

int create_clients(const struct settings *settings, void *data) {
	unsigned int i;

	assert ( settings != NULL );
	assert ( creq != NULL );

	for (i = 0; i < creq_size; i++) {
		cpu_set_t cpus;

		assert ( creq[i].settings != NULL );

		// If we are in quiet mode, we don't send anything, so lets not even create client thread
		if (creq[i].settings->quiet) {
			// But we must still say we are ready
			threads_signal_parent ( SIGNAL_READY_TO_GO, settings->threaded_model );
			continue;
		}

		cpu_setup( &cpus, creq[i].cores );

		//For now let's keep the client using the threaded model
		if ( create_thread( client_thread, &creq [i] , sizeof(cpus), &cpus, settings->threaded_model) ) {
			fprintf(stderr, "%s:%d create_thread() error\n", __FILE__, __LINE__ );
			return -1;
		}
	}
	return 0;
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
				free ( c );
				c = nextC;
			}
		}

		free( creq );
		creq = NULL;
		creq_size = 0;
	}
}
