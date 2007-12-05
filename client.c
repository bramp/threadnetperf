#include "common.h"
#include "global.h"

/**
	Creates n client connects to address
*/
void* client_thread(void *data) {
	const struct client_request *req = data;

	SOCKET client [ FD_SETSIZE ];
	SOCKET *c = client;
	SOCKET s;
	int clients = 0; // The number of clients
	int i;
	char *buffer = NULL;
	struct timespec waittime = {0, 100000000}; // 100 milliseconds

#ifdef _DEBUG
	printf("Started client thread %s %d\n",inet_ntoa(req->addr.sin_addr), req->n );
#endif

	// Blank client before we start
	for ( c = client; c < &client[ sizeof(client) / sizeof(*client) ]; c++)
		*c = INVALID_SOCKET;

	// Connect all the clients
	i = req->n;
	while ( i > 0 ) {
		//s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		s = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if ( s == INVALID_SOCKET ) {
			fprintf(stderr, "%s: %d socket() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		if ( disable_nagles ) {
			if ( disable_nagle( s ) == SOCKET_ERROR ) {
				fprintf(stderr, "%s: %d disable_nagle() error %d\n", __FILE__, __LINE__, ERRNO );
				goto cleanup;
			}
		}

		if ( connect( s, (const struct sockaddr *)&req->addr, sizeof(req->addr) ) == SOCKET_ERROR ) {
			fprintf(stderr, "%s: %d connect() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		client [ clients ] = s;
		clients++;

		i--;
	}

	buffer = malloc( message_size );
	memset( buffer, 0x41414141, message_size );

	// Wait for the go
	pthread_mutex_lock( &go_mutex );
	unready_threads--;
	while ( bRunning && unready_threads > 0 ) {
		pthread_cond_timedwait( &go_cond, &go_mutex, &waittime);
	}
	pthread_mutex_unlock( &go_mutex );

	// Now start the main loop
	while ( bRunning ) {
		fd_set readFD;
		fd_set writeFD;
		int ret;
		struct timeval waittime = {0, 100}; // 100 microseconds
		SOCKET nfds = INVALID_SOCKET;

		FD_ZERO ( &readFD ); FD_ZERO ( &writeFD );

		// Loop all client sockets
		for (c = client ; c < &client [ clients ] ; c++) {
			s = *c;
			assert ( s != INVALID_SOCKET );

			// Add them to FD sets
			FD_SET( s, &readFD);
			FD_SET( s, &writeFD);

			if ( s > nfds )
				nfds = s;
		}

		ret = select((int)(nfds + 1), &readFD, &writeFD, NULL, &waittime);
		if ( ret ==  SOCKET_ERROR ) {
			fprintf(stderr, "%s: %d select() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		// Figure out which sockets have fired
		i = 0;
		while ( ret > 0 ) {
			SOCKET s = client [ i ];

			assert ( i < sizeof( client ) / sizeof( *client) );
			assert ( s != INVALID_SOCKET );

			// Check for reads
			if ( FD_ISSET( s, &readFD) ) {
				int len = recv( s, buffer, message_size, 0);

				ret--;

				if ( len == SOCKET_ERROR ) {
					if ( ERRNO != ECONNRESET ) {
						fprintf(stderr, "%s: %d recv() error %d\n", __FILE__, __LINE__, ERRNO );
						goto cleanup;
					}
				} 

				// The socket has closed
				if ( len <= 0 ) {
					// Invalid this client
					closesocket( s );
					move_down ( &client[ i ], &client[ clients ] );
					clients--;

					// Quickly check if this client was in the write set
					if ( FD_ISSET( s, &writeFD) )
						ret--;

					continue;
				}
			}

			// Check if we are ready to write
			if ( FD_ISSET( s, &writeFD) ) {
				ret--;

				if ( send( s, buffer, message_size, 0 ) == SOCKET_ERROR ) {
					fprintf(stderr, "%s: %d send() error %d\n", __FILE__, __LINE__, ERRNO );
					goto cleanup;
				}
			}

			// Move the socket on
			i++;
		}
	}

cleanup:
	// Cleanup
	if ( buffer )
		free( buffer );

	// Shutdown client sockets
	for (c = client ; c < &client [ clients ] ; c++) {
		s = *c;
		if ( s != INVALID_SOCKET ) {
			shutdown ( s, SHUT_RDWR );
			closesocket( s );
		}
	}

	return NULL;
}
