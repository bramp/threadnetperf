#include "common.h"
#include "global.h"

void print_results( struct server_request *req ) {
	float thruput = req->bytes_received > 0 ? (float)req->bytes_received / (float)req->duration : 0;
	float duration = (float)req->duration / (float)1000000;

#ifdef WIN32 // Work around a silly windows bug in handling %llu
	printf( "Received %I64u bytes in %I64u pkts over %.2fs @ %.2f Mbytes/second\n", req->bytes_received, req->pkts_received, duration, thruput );
#else
	printf( "Received %llu bytes in %llu pkts over %.2fs @ %.2f Mbytes/second\n", req->bytes_received, req->pkts_received, duration, thruput );
#endif
}

/**
	Wait for and accept N connections
**/
int accept_connections(SOCKET listen, SOCKET *clients, int n) {

	assert ( listen != INVALID_SOCKET );
	assert ( clients != NULL );
	assert ( n > 0 );

	// Wait for all connections
	while ( bRunning && n > 0 ) {
		fd_set readFD;
		struct timeval waittime = {1, 0}; // 1 second
		int ret;
		struct sockaddr_storage addr;
		socklen_t addr_len = sizeof(addr);
		SOCKET s;

		FD_ZERO( &readFD );
		FD_SET( listen, &readFD);

		ret = select ( (int)listen + 1, &readFD, NULL, NULL, &waittime );
		if ( ret == SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d select() error %d\n", __FILE__, __LINE__, ERRNO );
			return 1;
		}

		#ifdef _DEBUG
		if ( ret == 0 )
			fprintf(stderr, "%s:%d select() timeout occured\n", __FILE__, __LINE__ );
		#endif

		// Did the listen socket fire?
		if ( ! FD_ISSET(listen, &readFD) ) {
			fprintf(stderr, "%s:%d FD_ISSET() has an invalid socket firing\n", __FILE__, __LINE__ );
			return 1;
		}

		// Accept a new client socket
		s = accept( listen, (struct sockaddr *)&addr, &addr_len );

		if ( s == INVALID_SOCKET ) {
			fprintf(stderr, "%s:%d accept() error %d\n", __FILE__, __LINE__, ERRNO );
			return 1;
		}

		if ( disable_nagles ) {
			if ( disable_nagle( s ) == SOCKET_ERROR ) {
				fprintf(stderr, "%s:%d disable_nagle() error %d\n", __FILE__, __LINE__, ERRNO );
				return 1;
			}
		}

		assert ( *clients == INVALID_SOCKET );
		*clients = s;
		++clients;

		#ifdef _DEBUG
		printf("New client %s (%d)\n", inet_ntoa(((struct sockaddr_in *)&addr)->sin_addr), n );
		#endif

		n--;
	}

	return 0;
}

/**
	Creates a server, and handles each incoming client
*/
void *server_thread(void *data) {
	struct server_request *req = data;

	SOCKET s = INVALID_SOCKET; // The listen server socket

	SOCKET client [ FD_SETSIZE ];
	SOCKET *c = client;
	int clients = req->n; // The number of clients

	int i;
	unsigned long long bytes_recv [ FD_SETSIZE ]; // Bytes received from each socket
	unsigned long long pkts_recv [ FD_SETSIZE ]; // Bytes received from each socket

	char *buffer = NULL; // Buffer to read data into, will be malloced later
	struct sockaddr_in addr; // Address to listen on

	struct timespec waittime = {0, 100000000}; // 100 milliseconds

	long long start_time; // The time we started
	long long end_time; // The time we ended

	int nfds;

#ifdef _DEBUG
	printf("Started server thread %d\n", req->port );
#endif

	// Blank client before we start
	for ( c = client; c < &client[ sizeof(client) / sizeof(*client) ]; c++)
		*c = INVALID_SOCKET;

	memset( bytes_recv, 0, sizeof(bytes_recv) );
	memset( pkts_recv, 0, sizeof(pkts_recv) );

	if ( req->n > sizeof(client) / sizeof(*client) ) {
		fprintf(stderr, "%s:%d server_thread() error Server thread can have no more than %d connections\n", __FILE__, __LINE__, sizeof(client) / sizeof(*client) );
		goto cleanup;
	}


	//s = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	s = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if ( s == INVALID_SOCKET ) {
		fprintf(stderr, "%s:%d socket() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( disable_nagles ) {
		if ( disable_nagle( s ) == SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d disable_nagle() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( req->port );

	if ( bind( s, (struct sockaddr *) &addr, sizeof(addr)) == SOCKET_ERROR) {
		fprintf(stderr, "%s:%d bind() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( listen(s, SOMAXCONN) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d listen() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	// Setup the buffer
	buffer = malloc( message_size );
	if ( buffer == NULL ) {
		fprintf(stderr, "%s:%d malloc() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( accept_connections(s, client, req->n) )
		goto cleanup;

	nfds = (int)*client;
	// Quickly loop to find the biggest socket
	for (c = client + 1 ; c < &client [clients] ; ++c)
		if ( (int)*c > nfds )
			nfds = (int)*c;

	nfds = nfds + 1;

	// Wait for the go
	pthread_mutex_lock( &go_mutex );
	unready_threads--;
	while ( bRunning && unready_threads > 0 ) {
		pthread_cond_timedwait( &go_cond, &go_mutex, &waittime);
	}
	pthread_mutex_unlock( &go_mutex );

	// Start timing
	start_time = get_microseconds();

	while ( bRunning ) {
		fd_set readFD;
		struct timeval waittime = {1, 0}; // 1 second
		int ret;

		FD_ZERO( &readFD );

		// Add all the client sockets
		for (c = client ; c < &client [clients] ; c++) {
			assert ( *c != INVALID_SOCKET );
			FD_SET( *c, &readFD);
		}

		ret = select( nfds, &readFD, NULL, NULL, &waittime);
		if ( ret ==  SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d select() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		#ifdef _DEBUG
		if ( ret == 0 )
			fprintf(stderr, "%s:%d select() timeout occured\n", __FILE__, __LINE__ );
		#endif

		// Figure out which sockets have fired
		i = 0;
		while ( ret > 0 ) {
			SOCKET s = client [ i ];

			assert ( i < sizeof( client ) / sizeof( *client) );
			assert ( s  != INVALID_SOCKET );

			// Check for reads
			if ( FD_ISSET( s, &readFD) ) {
				int len = recv( s, buffer, message_size, 0);

				ret--;

				// The socket has closed (or an error has occured)
				if ( len <= 0 ) {

					if ( len == SOCKET_ERROR && ERRNO != ECONNRESET ) {
						fprintf(stderr, "%s:%d recv() error %d\n", __FILE__, __LINE__, ERRNO );
						goto cleanup;
					}

					#ifdef _DEBUG
					printf("Remove client (%d/%d)\n", i, clients );
					#endif

					// Invalidate this client
					closesocket( s );
					move_down ( &client[ i ], &client[ clients ] );
					clients--;
					continue;

				} else {
					// We could dirty the buffer

					// Count how many bytes have been received
					bytes_recv [ i ] += len;
					pkts_recv [ i ] ++;
				}
			}

			// Move the socket on (if needed)
			i++;
		}
	}

	// We have finished, work out some stats
	end_time = get_microseconds();

	// Add up all the client bytes
	req->duration = end_time - start_time;
	req->bytes_received = 0;
	req->pkts_received = 0;
	for (i = 0 ; i <  sizeof(bytes_recv) / sizeof(*bytes_recv); i++) {
		req->bytes_received += bytes_recv [ i ];
		req->pkts_received += pkts_recv [ i ];
	}

	print_results(req);

cleanup:
	// Force a stop
	bRunning = 0;

	// Cleanup
	if ( buffer )
		free( buffer );

	// Shutdown server socket
	if ( s != INVALID_SOCKET ) {
		shutdown ( s, SHUT_RDWR );
		closesocket( s );
	}

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