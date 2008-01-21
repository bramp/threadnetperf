#include "client.h"

#include "global.h"

#include "netlib.h"

#include <assert.h>
#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>

#ifndef WIN32
#include <unistd.h>
#endif

SOCKET client [ FD_SETSIZE ];
int clients = 0; // The number of clients

int connect_connections(const struct settings *settings, const struct client_request * req) {

	const struct client_request_details * details = req->details;

	// Loop all the client requests for this thread
	while ( details != NULL ) {
		unsigned int i;

		if ( settings->verbose ) {
			char addr[NI_MAXHOST + NI_MAXSERV + 1];

			// Print the host/port
			addr_to_ipstr(details->addr, details->addr_len, addr, sizeof(addr));

			printf("  Core %d: Connecting %d client%s to %s\n", 
				req->core, details->n, details->n > 1 ? "s" : "", 
				addr);
		}

		// Connect all the clients
		i = details->n;
		while ( i > 0 ) {
			int send_socket_size, recv_socket_size;
			SOCKET s;

			if ( clients >= sizeof(client) / sizeof(*client) ) {
				fprintf(stderr, "%s:%d client_thread() error Client thread can have no more than %d connections\n", __FILE__, __LINE__, (int)(sizeof(client) / sizeof(*client)) );
				return -1;
			}

			s = socket( AF_INET, settings->type, settings->protocol);
			if ( s == INVALID_SOCKET ) {
				fprintf(stderr, "%s:%d socket() error %d\n", __FILE__, __LINE__, ERRNO );
				return -1;
			}

	 		send_socket_size = set_socket_send_buffer( s, settings->socket_size );
			if ( send_socket_size < 0 ) {
				fprintf(stderr, "%s:%d set_socket_send_buffer() error %d\n", __FILE__, __LINE__, ERRNO );
				goto cleanup;
			}
			
	 		recv_socket_size = set_socket_recv_buffer( s, settings->socket_size );
			if ( send_socket_size < 0 ) {
				fprintf(stderr, "%s:%d set_socket_recv_buffer() error %d\n", __FILE__, __LINE__, ERRNO );
				goto cleanup;
			}
			
			if ( settings->verbose ) {
				// TODO tidy this
				printf("client socket size: %d/%d\n", send_socket_size, recv_socket_size );
			}

			if ( settings->disable_nagles ) {
				if ( disable_nagle( s ) == SOCKET_ERROR ) {
					fprintf(stderr, "%s:%d disable_nagle() error %d\n", __FILE__, __LINE__, ERRNO );
					goto cleanup;
				}
			}

			if ( set_socket_timeout(s, CONTROL_TIMEOUT) ) {
				fprintf(stderr, "%s:%d set_socket_timeout() error %d\n", __FILE__, __LINE__, ERRNO );
				goto cleanup;
			}

			if ( connect( s, details->addr, details->addr_len ) == SOCKET_ERROR ) {
				fprintf(stderr, "%s:%d connect() error %d\n", __FILE__, __LINE__, ERRNO );
				goto cleanup;
			}

			// Always disable blocking (to work around linux bug)
			if ( disable_blocking(s) == SOCKET_ERROR ) {
				fprintf(stderr, "%s:%d disable_blocking() error %d\n", __FILE__, __LINE__, ERRNO );
				goto cleanup;
			}

			client [ clients ] = s;
			clients++;

			i--;
			continue;

		cleanup:
			closesocket(s);
			return -1;
		}

		// move onto the next client request
		details = details->next;
	}

	return 0;
}

/**
	Creates n client connects to address
*/
void* client_thread(void *data) {
	const struct client_request * const req = data;
	const struct client_request_details * details = req->details;

	// Make a copy of the global settings
	const struct settings settings = *req->settings;
	
	// Pointer to the end of the buffer
	unsigned long long* end_buffer = NULL ;
	
	SOCKET *c = NULL;

	char *buffer = NULL;
	struct timespec waittime = {0, 100000000}; // 100 milliseconds
	int nfds;

	fd_set readFD;
	fd_set writeFD;

	assert ( req != NULL );
	assert ( details != NULL );

	clients = 0;

	// Blank client before we start
	for ( c = client; c < &client[ sizeof(client) / sizeof(*client) ]; c++)
		*c = INVALID_SOCKET;

	if ( settings.verbose )
		printf("Core %d: Started client thread\n", req->core);

	if ( connect_connections(&settings, req) ) {
		goto cleanup;
	}

	buffer = malloc( settings.message_size );
	memset( buffer, (int)BUFFER_FILL, settings.message_size );

	if (settings.timestamp && settings.message_size > sizeof(*end_buffer) )
		end_buffer = (unsigned long long *) &buffer[settings.message_size - sizeof(*end_buffer) ];

	nfds = (int)*client;
	FD_ZERO ( &readFD ); FD_ZERO ( &writeFD );

	// Loop all client sockets
	for (c = client ; c < &client [ clients ] ; c++) {
		assert ( *c != INVALID_SOCKET );

		// Add them to FD sets
		FD_SET( *c, &readFD);
		FD_SET( *c, &writeFD);

		if ( (int)*c > nfds )
			nfds = (int)*c;
	}

	nfds = nfds + 1;

	pthread_mutex_lock( &go_mutex );
	unready_threads--;

	 // Signal we are ready
	pthread_mutex_lock( &ready_mutex );
	pthread_cond_signal( &ready_cond );
	pthread_mutex_unlock( &ready_mutex );

	// Wait for the go
	while ( req->bRunning && !bGo ) {
		pthread_cond_timedwait( &go_cond, &go_mutex, &waittime);
	}
	pthread_mutex_unlock( &go_mutex );

	// Now start the main loop
	while ( req->bRunning ) {

		int ret;
		struct timeval waittime = {1, 0}; // 1 second

		ret = select(nfds, &readFD, &writeFD, NULL, &waittime);
		if ( ret ==  SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d select() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		#ifdef _DEBUG
		if ( ret == 0 )
			fprintf(stderr, "%s:%d select() timeout occured\n", __FILE__, __LINE__ );
		#endif

		// Figure out which sockets have fired
		for (c = client ; c < &client [ clients ]; c++ ) {
			SOCKET s = *c;

			assert ( s != INVALID_SOCKET );

			// Speed hack
			if ( ret == 0 ) {
				FD_SET( s, &readFD);
				FD_SET( s, &writeFD);
				continue;
			}

			// Check for reads
			if ( FD_ISSET( s, &readFD) ) {
				int len = recv( s, buffer, settings.message_size, 0);

				ret--;

				if ( len == SOCKET_ERROR ) {
					if ( ERRNO != ECONNRESET ) {
						fprintf(stderr, "%s:%d recv() error %d\n", __FILE__, __LINE__, ERRNO );
						goto cleanup;
					}
				}

				// The socket has closed
				if ( len <= 0 ) {

					// Quickly check if this client was in the write set
					if ( FD_ISSET( s, &writeFD) ) {
						FD_CLR( s, &writeFD );
						ret--;
					}

					if ( settings.verbose )
						printf("  Client: %d Remove client (%d/%d)\n", req->core, (int)((c - client) / sizeof(*c)), clients );

					// Unset me from the set
					FD_CLR( s, &readFD );

					// Invalid this client
					closesocket( s );
					move_down ( c, &client[ clients ] );
					clients--;

					// Move this back
					c--;

					// Update the nfds
					nfds = (int)highest_socket(client, clients) + 1;

					continue;
				}
			} else {
				// Set the socket on this FD, to save us doing it at the beginning of each loop
				FD_SET( s, &readFD);
			}

			// Check if we are ready to write
			if ( FD_ISSET( s, &writeFD) ) {
				ret--;
				
				if (end_buffer != NULL) {
					*end_buffer = get_microseconds();
				}

				if ( send( s, buffer, settings.message_size, 0 ) == SOCKET_ERROR ) {
					if ( ERRNO != EWOULDBLOCK && ERRNO != EPIPE ) {
						fprintf(stderr, "%s:%d send() error %d\n", __FILE__, __LINE__, ERRNO );
						goto cleanup;
					}
				}
			} else {
				// Set the socket on this FD, to save us doing it at the beginning of each loop
				FD_SET( s, &writeFD);
			}

		}
	}

cleanup:

	// Force a stop
	stop_all();

	// Cleanup
	if ( buffer )
		free( buffer );

	// Shutdown client sockets
	for (c = client ; c < &client [ clients ] ; c++) {
		if ( *c != INVALID_SOCKET ) {
			shutdown ( *c, SHUT_RDWR );
			closesocket( *c );
		}
	}

	return NULL;
}
