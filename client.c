#include "common.h"
#include "global.h"

/**
	Creates n client connects to address
*/
void* client_thread(void *data) {
	const struct client_request *req = data;
	
	// Make a copy of the global settings
	const struct settings settings = global_settings;
	
	//Const pointer to the end of the buffer
	unsigned long long* end_buffer = NULL ;
	
	SOCKET client [ FD_SETSIZE ];
	SOCKET *c = client;
	SOCKET s;
	int clients = 0; // The number of clients
	int i;
	char *buffer = NULL;
	struct timespec waittime = {0, 100000000}; // 100 milliseconds
	int nfds;

	char msg[1024]; // Debug message for each set of connected clients
	const size_t msg_max_len = sizeof(msg) / sizeof(*msg);
	size_t msg_len = 0;

	fd_set readFD;
	fd_set writeFD;

	assert ( req != NULL );

	// Blank client before we start
	for ( c = client; c < &client[ sizeof(client) / sizeof(*client) ]; c++)
		*c = INVALID_SOCKET;

	if ( settings.verbose )
		msg_len += sprintf(msg, "Core %d: Started client thread ", req->core);

	// Loop all the client requests for this thread
	while ( req != NULL ) {

		if ( settings.verbose ) {
			// Print the host/port
			msg_len += sprintf(msg + msg_len, "%d(", req->n );
			msg_len += addr_to_ipstr(req->addr, req->addr_len, msg + msg_len, msg_max_len - msg_len);
			msg_len += sprintf(msg + msg_len, ":%u) ", ntohs( ((struct sockaddr_in *)req->addr)->sin_port) );
		}

		// Connect all the clients
		i = req->n;
		while ( i > 0 ) {
			int send_socket_size, recv_socket_size;
			
			if ( clients >= sizeof(client) / sizeof(*client) ) {
				fprintf(stderr, "%s:%d client_thread() error Client thread can have no more than %d connections\n", __FILE__, __LINE__, (int)(sizeof(client) / sizeof(*client)) );
				goto cleanup;
			}

			s = socket( AF_INET, settings.type, settings.protocol);
			if ( s == INVALID_SOCKET ) {
				fprintf(stderr, "%s:%d socket() error %d\n", __FILE__, __LINE__, ERRNO );
				goto cleanup;
			}

	 		send_socket_size = set_socket_send_buffer( s, settings.socket_size );
			if ( send_socket_size < 0 ) {
				fprintf(stderr, "%s:%d set_socket_send_buffer() error %d\n", __FILE__, __LINE__, ERRNO );
				goto cleanup;
			}
			
	 		recv_socket_size = set_socket_recv_buffer( s, settings.socket_size );
			if ( send_socket_size < 0 ) {
				fprintf(stderr, "%s:%d set_socket_recv_buffer() error %d\n", __FILE__, __LINE__, ERRNO );
				goto cleanup;
			}
			
			if ( settings.verbose ) {
				// TODO tidy this
				printf("client socket size: %d/%d\n", send_socket_size, recv_socket_size );
			}

			if ( settings.disable_nagles ) {
				if ( disable_nagle( s ) == SOCKET_ERROR ) {
					fprintf(stderr, "%s:%d disable_nagle() error %d\n", __FILE__, __LINE__, ERRNO );
					goto cleanup;
				}
			}

			if ( connect( s, req->addr, req->addr_len ) == SOCKET_ERROR ) {
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
		}

		// move onto the next client request
		req = req->next;
	}

	if ( settings.verbose )
		printf("%s\n", msg);

	buffer = malloc( settings.message_size );
	end_buffer = (unsigned long long *) &buffer[settings.message_size - sizeof(unsigned long long)] ;
	memset( buffer, 0x41414141, settings.message_size );


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

	// Wait for the go
	pthread_mutex_lock( &go_mutex );
	unready_threads--;
	while ( bRunning && unready_threads > 0 ) {
		pthread_cond_timedwait( &go_cond, &go_mutex, &waittime);
	}
	pthread_mutex_unlock( &go_mutex );

	// Now start the main loop
	while ( bRunning ) {

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

					// Unset me from the set
					FD_CLR( s, &readFD );

					// Invalid this client
					closesocket( s );
					move_down ( c, &client[ clients ] );
					clients--;

					// Move this back
					c--;

					continue;
				}
			} else {
				// Set the socket on this FD, to save us doing it at the beginning of each loop
				FD_SET( s, &readFD);
			}

			// Check if we are ready to write
			if ( FD_ISSET( s, &writeFD) ) {
				int send_len;
				ret--;
				
				if (settings.timestamp) {
					unsigned long long now = get_microseconds();
					*end_buffer = now;
				}

				if ( (send_len=send( s, buffer, settings.message_size, 0 )) == SOCKET_ERROR ) {
					fprintf(stderr, "%s:%d send() error %d\n", __FILE__, __LINE__, ERRNO );
					goto cleanup;
				}
			//	printf("send len %d\n", send_len);	
			} else {
				// Set the socket on this FD, to save us doing it at the beginning of each loop
				FD_SET( s, &writeFD);
			}

		}
	}

cleanup:

	// Force a stop
	bRunning = 0;

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

	//pthread_exit( NULL );
	return NULL;
}
