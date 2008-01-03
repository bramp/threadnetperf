#include "common.h"
#include "global.h"



/**
	Wait for and accept N connections
**/
int accept_connections(int servercore, SOCKET listen, SOCKET *clients, int n) {

	int connected = 0;
	
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

		if ( ret == 0 )
			continue;

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
		connected++;

		if ( verbose )
			printf("  Server %d incoming client %s (%d)\n", servercore, inet_ntoa(((struct sockaddr_in *)&addr)->sin_addr), connected );

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
	unsigned long long pkts_recv [ FD_SETSIZE ]; // Number of recv calls from each socket

	unsigned long long pkts_time [ FD_SETSIZE ]; // Total time packets spent (in network) for each socket (used in timestamping)

	char *buffer = NULL; // Buffer to read data into, will be malloced later
	struct sockaddr_in addr; // Address to listen on

	struct timespec waittime = {0, 100000000}; // 100 milliseconds

	long long start_time; // The time we started
	long long end_time; // The time we ended

	int one = 1;

	int nfds;

	if ( verbose )
		printf("Core %d: Started server thread port %d\n", req->core, req->port );

	// Blank client before we start
	for ( c = client; c < &client[ sizeof(client) / sizeof(*client) ]; c++)
		*c = INVALID_SOCKET;

	memset( bytes_recv, 0, sizeof(bytes_recv) );
	memset( pkts_recv, 0, sizeof(pkts_recv) );
	memset( pkts_time, 0, sizeof(pkts_time) );

	if ( req->n > sizeof(client) / sizeof(*client) ) {
		fprintf(stderr, "%s:%d server_thread() error Server thread can have no more than %d connections\n", __FILE__, __LINE__, (int)(sizeof(client) / sizeof(*client)) );
		goto cleanup;
	}

	// Create the listen socket
	s = socket( PF_INET, type, protocol);

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

	// SO_REUSEADDR
	if ( setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d setsockopt(SO_REUSEADDR) error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( req->port );

	if ( bind( s, (struct sockaddr *) &addr, sizeof(addr)) == SOCKET_ERROR) {
		fprintf(stderr, "%s:%d bind() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( (type == SOCK_STREAM || type==SOCK_SEQPACKET) && listen(s, SOMAXCONN) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d listen() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	// Setup the buffer
	buffer = malloc( message_size );
	if ( buffer == NULL ) {
		fprintf(stderr, "%s:%d malloc() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	// If this is a STREAM then accept each connection
	if ( type == SOCK_STREAM ) {
		if ( accept_connections(req->core, s, client, req->n) ) {
			goto cleanup;
		}

	// If this is a DGRAM, then we don't have a connection per client, but instead one server socket
	} else if ( type == SOCK_DGRAM ) {
		*client = s;
		clients = 1;
	}

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

					if ( verbose )
						printf("Remove client (%d/%d)\n", i, clients );

					// Invalidate this client
					closesocket( s );
					move_down ( &client[ i ], &client[ clients ] );
					clients--;
					continue;

				} else {
					if (timestamp) {
						unsigned long long us = *((unsigned long long *)buffer);
						pkts_time[ i ] += get_microseconds() - us;
					}

					// We could dirty the buffer
					if (dirty) {
						int *d;
						int temp;
						for (d=(int *)buffer; d<(int *)(buffer + len); d++)
							temp += *d;

						// Read temp to avoid this code being otomised out
						if ( temp )
							temp = 0;
					}
					// Count how many bytes have been received
					bytes_recv [ i ] += len;
					pkts_recv [ i ] ++;
				}
			}

			// Move the socket on
			i++;
		}
	}

	// We have finished, work out some stats
	end_time = get_microseconds();

	// Add up all the client bytes
	req->stats.duration = end_time - start_time;
	req->stats.bytes_received = 0;
	req->stats.pkts_received = 0;
	for (i = 0 ; i <  sizeof(bytes_recv) / sizeof(*bytes_recv); i++) {
		req->stats.bytes_received += bytes_recv [ i ];
		req->stats.pkts_received += pkts_recv [ i ];
		req->stats.pkts_time += pkts_time [ i ];
	}

	print_results(req->core, &req->stats);

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
		if ( *c != INVALID_SOCKET ) {
			shutdown ( *c, SHUT_RDWR );
			closesocket( *c );
		}
	}

	//pthread_exit( NULL );
	return NULL;
}
