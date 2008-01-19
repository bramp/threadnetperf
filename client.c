#include "client.h"
#include "global.h"

#include <assert.h>
#include <malloc.h>
#include <stdio.h>

// Array of all the client requests
struct client_request *creq = NULL;
size_t creq_size = 0;

int prepare_clients(const struct settings * settings) {

	unsigned int servercore, clientcore;
	unsigned int ** clientserver;

	assert ( settings != NULL );
	assert ( creq == NULL );
	assert ( creq_size == 0 );

	// Malloc one space for each core
	creq_size = settings->cores;
	creq = calloc ( creq_size, sizeof(*creq) );
	if ( !creq ) {
		creq_size = 0;
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		return 1;
	}

	clientserver = settings->clientserver;
	assert ( clientserver != NULL );

	// Loop through clientserver looking for each server we need to create
	for (servercore = 0; servercore < settings->cores; servercore++) {
		for (clientcore = 0; clientcore < settings->cores; clientcore++) {

			struct client_request_details *c;

			// Don't bother if there are zero requests
			if ( clientserver [ clientcore ] [ servercore ] == 0 )
				continue;

			// Check if we haven't set up this client thread yet
			if ( creq [ clientcore ].bRunning == 0 ) {
				creq [ clientcore ].bRunning = 1;
				creq [ clientcore ].settings = settings;
				creq [ clientcore ].core = clientcore;

				unready_threads++;
			} 

			// Malloc the request details
			c = calloc( 1, sizeof( *c ) );
			if ( !c ) {
				fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
				return 1;
			}

			// Add this new details before the other details
			c->next = creq [ clientcore ].details;
			creq [ clientcore ].details = c;

			c->n = clientserver [ clientcore ] [ servercore ];

			// Create the client dest addr
			c->addr_len = sizeof ( struct sockaddr_in );

			c->addr = calloc ( 1, c->addr_len ) ;
			if ( !c->addr ) {
				fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
				return 1;
			}

			// Change this to be more address indepentant
			((struct sockaddr_in *)c->addr)->sin_family = AF_INET;
			((struct sockaddr_in *)c->addr)->sin_addr.s_addr = inet_addr( settings->server_host );
			((struct sockaddr_in *)c->addr)->sin_port = htons( settings->port + servercore );

			if ( ((struct sockaddr_in *)c->addr)->sin_addr.s_addr == INADDR_NONE ) {
				fprintf(stderr, "Invalid host name (%s)\n", settings->server_host );
				return 1;
			}
		}
	}

	return 0;
}

int create_clients(const struct settings *settings) {
	unsigned int clientcore;

	assert ( settings != NULL );
	assert ( creq != NULL );

	for (clientcore = 0; clientcore < settings->cores; clientcore++) {
		cpu_set_t cpus;

		if ( ! creq[clientcore].bRunning )
			continue;

		CPU_ZERO ( &cpus );
		CPU_SET ( clientcore, &cpus );

		if ( pthread_create_on( &thread[threads], NULL, client_thread, &creq [clientcore] , sizeof(cpus), &cpus) ) {
			fprintf(stderr, "%s:%d pthread_create_on() error\n", __FILE__, __LINE__ );
			return 1;
		}

		threads++;
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
