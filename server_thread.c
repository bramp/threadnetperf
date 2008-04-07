#include "server.h"

#include "global.h"
#include "print.h"
#include "netlib.h"

#include <assert.h>
#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>

#ifndef WIN32
#include <unistd.h>
#endif

// Count of how many threads are not listening
volatile unsigned int server_listen_unready = 0;

/**
	Wait for and accept N connections
**/
int accept_connections(const struct server_request *req, SOCKET listen, SOCKET *clients) {

	const struct settings *settings = req->settings;

	unsigned int n = req->n;
	int connected = 0;
	fd_set readFD;

	assert ( listen != INVALID_SOCKET );
	assert ( clients != NULL );
	assert ( req->n > 0 );

	FD_ZERO(&readFD);

	// Wait for all connections
	while ( req->bRunning && n > 0 ) {
		struct timeval waittime = {CONTROL_TIMEOUT / 1000, 0};
		int ret;
		struct sockaddr_storage addr;
		socklen_t addr_len = sizeof(addr);
		SOCKET s;
		int send_socket_size, recv_socket_size;

		FD_SET( listen, &readFD);

		ret = select ( (int)listen + 1, &readFD, NULL, NULL, &waittime );
		if ( ret <= 0 ) {
			if (ERRNO != 0)
				fprintf(stderr, "%s:%d select() error %d\n", __FILE__, __LINE__, ERRNO );
			return 1;
		}

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

		send_socket_size = set_socket_send_buffer( s, settings->socket_size );
		if ( send_socket_size < 0 ) {
			fprintf(stderr, "%s:%d set_socket_send_buffer() error %d\n", __FILE__, __LINE__, ERRNO );
			return 1;
		}

		recv_socket_size = set_socket_recv_buffer( s, settings->socket_size );
		if ( send_socket_size < 0 ) {
			fprintf(stderr, "%s:%d set_socket_recv_buffer() error %d\n", __FILE__, __LINE__, ERRNO );
			return 1;
		}

		if ( settings->disable_nagles ) {
			if ( disable_nagle( s ) == SOCKET_ERROR ) {
				fprintf(stderr, "%s:%d disable_nagle() error %d\n", __FILE__, __LINE__, ERRNO );
				return 1;
			}
		}

		// If we are timestamping get a timestamp in advance to make sure the kernel is going to timestamp it
		if ( settings->timestamp ) {
			get_packet_timestamp(s);
		}

		// Always disable blocking (to work around linux bug)
		if ( disable_blocking(s) == SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d disable_blocking() error %d\n", __FILE__, __LINE__, ERRNO );
			return 1;
		}

		assert ( *clients == INVALID_SOCKET );
		*clients = s;
		++clients;
		connected++;

		if ( settings->verbose )
			printf("  Server: %d incoming client %s (%d) socket size: %d/%d\n",
				req->core, inet_ntoa(((struct sockaddr_in *)&addr)->sin_addr), connected,
				send_socket_size, recv_socket_size );

		n--;
	}

	return 0;
}

/**
	Creates a server, and handles each incoming client
*/
void *server_thread(void *data) {
	struct server_request * const req = data;

	// Copy the global settings
	const struct settings settings = *req->settings;

	SOCKET s = INVALID_SOCKET; // The listen server socket

	SOCKET client [ FD_SETSIZE ];
	SOCKET *c = client;
	int clients = req->n; // The number of clients

	int return_stats = 0; // Should we return the stats?

	unsigned int i;
	unsigned long long bytes_recv [ FD_SETSIZE ]; // Bytes received from each socket
	unsigned long long pkts_recv [ FD_SETSIZE ]; // Number of recv calls from each socket

	unsigned long long pkts_time [ FD_SETSIZE ]; // Total time packets spent (in network) for each socket (used in timestamping)

	struct msghdr msgs;
	struct iovec msg_iov = {NULL, 0}; // Buffer to read data into, will be malloced later

	struct sockaddr_in addr; // Address to listen on

	struct timespec waittime = {0, 100000000}; // 100 milliseconds

	long long start_time; // The time we started
	long long end_time; // The time we ended

	int send_socket_size, recv_socket_size; // The socket buffer sizes

	fd_set readFD;

	int one = 1;

	int nfds;

	if ( settings.verbose )
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
	s = socket( PF_INET, settings.type, settings.protocol);

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
	if ( recv_socket_size < 0 ) {
		fprintf(stderr, "%s:%d set_socket_recv_buffer() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( settings.disable_nagles ) {
		if ( disable_nagle( s ) == SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d disable_nagle() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}
	}

	// If we are timestamping get a timestamp in advance to make sure the kernel is going to timestamp it
	if ( settings.timestamp ) {
		// TODO change this to enable timestamping
		get_packet_timestamp(s);
	}

	// SO_REUSEADDR
	if ( setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d setsockopt(SOL_SOCKET, SO_REUSEADDR) error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( req->port );

	// Bind
	if ( bind( s, (struct sockaddr *) &addr, sizeof(addr)) == SOCKET_ERROR) {
		fprintf(stderr, "%s:%d bind() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	// Listen
	if ( (settings.type == SOCK_STREAM || settings.type==SOCK_SEQPACKET) ) {
		if ( listen(s, SOMAXCONN) == SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d listen() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}
	}

	// We are now listening and waiting
	pthread_mutex_lock( &go_mutex );
	server_listen_unready--;
	pthread_mutex_unlock( &go_mutex );

	// If this is a STREAM then accept each connection
	if ( settings.type == SOCK_STREAM ) {
		// Wait until all connections have been accepted
		if ( accept_connections(req, s, client) ) {
			goto cleanup;
		}

	// If this is a DGRAM, then we don't have a connection per client, but instead one server socket
	} else if ( settings.type == SOCK_DGRAM ) {
		*client = s;
		clients = 1;
	}

	// By this point all the clients have connected, but the test hasn't started yet

	// Setup the buffer
	msg_iov.iov_len = settings.message_size;
	msg_iov.iov_base = malloc( settings.message_size );
	if ( msg_iov.iov_base == NULL ) {
		fprintf(stderr, "%s:%d malloc() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	msgs.msg_name = NULL;
	msgs.msg_namelen = 0;
	msgs.msg_iov = &msg_iov;
	msgs.msg_iovlen = 1;
	msgs.msg_control = NULL;
	msgs.msg_controllen = 0;
	msgs.msg_flags = 0;

	// Setup FD_SETs
	FD_ZERO( &readFD );
	nfds = (int)*client;

	// Add all the client sockets to the fd_set
	for (c = client ; c < &client [clients] ; c++) {
		assert ( *c != INVALID_SOCKET );

		FD_SET( *c, &readFD);
		if ( (int)*c > nfds )
			nfds = (int)*c;
	}
	nfds = nfds + 1;

	// Wait for the go
	pthread_mutex_lock( &go_mutex );
	unready_threads--;

	 // Signal we are ready
	pthread_mutex_lock( &ready_mutex );
	pthread_cond_signal( &ready_cond );
	pthread_mutex_unlock( &ready_mutex );

	// Wait for the go
	while ( req->bRunning && !bGo ) {
		// TODO FIX WATITIME, it is absolute time, not relative
		pthread_cond_timedwait( &go_cond, &go_mutex, &waittime);
	}
	pthread_mutex_unlock( &go_mutex );

	// Start timing
	start_time = get_microseconds();

	while ( req->bRunning ) {
		struct timeval waittime = {1, 0}; // 1 second
		int ret;

		ret = select( nfds, &readFD, NULL, NULL, &waittime);
		if ( ret ==  SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d select() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		#ifdef _DEBUG
		if ( ret == 0 && !req->bRunning )
			fprintf(stderr, "%s:%d select() timeout occured\n", __FILE__, __LINE__ );
		#endif

		// Figure out which sockets have fired
		for (c = client, i = 0 ; c < &client [ clients ]; c++ ) {
			SOCKET s = *c;

			assert ( s != INVALID_SOCKET );

			if ( ret == 0 ) {
				FD_SET( s, &readFD);
				continue;
			}

			// Check for reads
			if ( FD_ISSET( s, &readFD) ) {
				// TODO MSG_WAITALL
				int len = recvmsg( s, &msgs, 0);

				ret--;

				// The socket has closed (or an error has occured)
				if ( len <= 0 ) {

					if ( len == SOCKET_ERROR ) {
						int lastErr = ERRNO;

						// If it is a blocking error just continue
						if ( lastErr == EWOULDBLOCK )
							continue;

						else if ( lastErr != ECONNRESET ) {
							fprintf(stderr, "%s:%d recv() error %d\n", __FILE__, __LINE__, lastErr );
							goto cleanup;
						}
					}

					if ( settings.verbose )
						printf("  Server: %d Removed client (%d/%d)\n", req->core, i + 1, clients );

					FD_CLR( s, &readFD );

					// Invalidate this client
					closesocket( s );
					move_down ( c, &client[ clients ] );
					clients--;

					// Move back
					c--;

					// Update the nfds
					nfds = (int)highest_socket(client, clients) + 1;

					continue;

				} else {
					// We could dirty the buffer
					if (settings.dirty) {
						// These is volatile to stop the compiler removing this loop
						volatile int *d;
						volatile int temp = 0;
						for (d=(int *)msg_iov.iov_base; d<(int *)(msg_iov.iov_base + len); d++) {
							temp += *d;
						}
					}

					if ( settings.timestamp ) {
						const unsigned long long now = get_nanoseconds();
						const unsigned long long ns  = get_packet_timestamp(s);

						if(ns <= now) {
							pkts_time[ i ] += now - ns;
						} else {
							printf("%llu	%llu\n", now, ns);
							req->stats.time_err++;
						}

						#ifdef CHECK_TIMES
							if(pkts_recv [ i ] < CHECK_TIMES ) {
								req->stats.processed_something = 1;
								req->stats.processing_times[pkts_recv [ i ]] = t;
							}
						#endif
					}
					// Count how many bytes have been received
					bytes_recv [ i ] += len;
					pkts_recv [ i ] ++;
				}
			} else {
				// Set the socket on this FD, to save us doing it at the beginning of each loop
				FD_SET( s, &readFD);
			}

			i++;
		}
	}

	// We have finished, work out some stats
	end_time = get_microseconds();

	// Add up all the client bytes
	req->stats.core = req->core;
	req->stats.duration = end_time - start_time;
	req->stats.bytes_received = 0;
	req->stats.pkts_received = 0;
	for (i = 0 ; i <  sizeof(bytes_recv) / sizeof(*bytes_recv); i++) {
		req->stats.bytes_received += bytes_recv [ i ];
		req->stats.pkts_received += pkts_recv [ i ];
		req->stats.pkts_time += pkts_time [ i ];
	}
	return_stats = 1;

cleanup:
	// Force a stop
	stop_all();

	// Cleanup
	if ( msg_iov.iov_base )
		free( msg_iov.iov_base );

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

	if ( return_stats )
		return &req->stats;

	return NULL;
}
