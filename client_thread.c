#include "client.h"

#include "global.h"

#include "netlib.h"

#include <assert.h>
#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>

#ifdef USE_EPOLL
#include <sys/epoll.h>
#endif

#ifndef WIN32
#include <unistd.h>
#endif

int connect_connections(const struct settings *settings,
		const struct client_request * req, SOCKET *client,
		unsigned int *clients) {

	const struct client_request_details * details = req->details;

	// Loop all the client requests for this thread
	while (details != NULL) {
		unsigned int i = details->n;

		if (settings->verbose) {
			char addr[NI_MAXHOST + NI_MAXSERV + 1];

			// Print the host/port
			addr_to_ipstr((const struct sockaddr *)&details->addr,
					details->addr_len, addr, sizeof(addr));

			printf("  Core %d: Connecting %d client%s to %s\n", req->cores,
					details->n, details->n > 1 ? "s" : "", addr);
		}

		// Connect all the clients
		while (i > 0) {
			int send_socket_size, recv_socket_size;

			SOCKET s = socket( AF_INET, settings->type, settings->protocol);

			if (s == INVALID_SOCKET) {
				fprintf(stderr, "%s:%d socket() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO));
				return -1;
			}

#ifndef WIN32
#ifndef USE_EPOLL
			// In GNU world, a socket can't be >= FD_SETSIZE, otherwise it can't be placed into a set
			if ( s >= FD_SETSIZE ) {
				fprintf(stderr, "%s:%d socket() value too large for fd_set (%d >= %d)\n", __FILE__, __LINE__, s, FD_SETSIZE );
				return -1;
			}
#endif
#endif

			send_socket_size = set_socket_send_buffer(s, settings->socket_size);
			if (send_socket_size < 0) {
				fprintf(stderr, "%s:%d set_socket_send_buffer() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO));
				goto cleanup;
			}

			recv_socket_size = set_socket_recv_buffer(s, settings->socket_size);
			if (send_socket_size < 0) {
				fprintf(stderr, "%s:%d set_socket_recv_buffer() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO));
				goto cleanup;
			}

			if (settings->verbose) {
				// TODO tidy this
				printf("client socket size: %d/%d\n", send_socket_size,
						recv_socket_size);
			}

			if (settings->disable_nagles) {
				if (disable_nagle(s) == SOCKET_ERROR) {
					fprintf(stderr, "%s:%d disable_nagle() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO));
					goto cleanup;
				}

				//if ( enable_maxseq ( s , settings->message_size ) == SOCKET_ERROR ) {
				//	fprintf(stderr, "%s:%d enable_maxseq() error %d\n", __FILE__, __LINE__, ERRNO );
				//	goto cleanup;
				//}
			}

			if (set_socket_timeout(s, CONTROL_TIMEOUT) ) {
				fprintf(stderr, "%s:%d set_socket_timeout() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO));
				goto cleanup;
			}

			if (connect(s, (const struct sockaddr *)&details->addr, (int)details->addr_len ) == SOCKET_ERROR) {
				fprintf(stderr, "%s:%d connect() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO));
				goto cleanup;
			}

			// Always disable blocking (to work around linux bug)
			if (disable_blocking(s) == SOCKET_ERROR) {
				fprintf(stderr, "%s:%d disable_blocking() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO));
				goto cleanup;
			}

			assert ( s != INVALID_SOCKET );
			assert ( *client == INVALID_SOCKET );

			*client++ = s; // Add socket s to the end of the array and move along
			(*clients)++; // Increment the count of clients

			i--;
			continue;

			cleanup:
			// This cleanup section is within the loop so we can cleanup s
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

	// The time in microseconds to wait between each send (to limit our bandwidth)
	const unsigned long long time_between_sends = settings.rate> 0 ? 1000000
	/ settings.rate : 0;
	unsigned long long next_send_time = 0;

	// Array of client sockets
	SOCKET *client = NULL;
	unsigned int clients = 0; // The number of clients
	unsigned int clients_temp = 0; // The number of clients

	SOCKET *c = NULL;

	char *buffer= NULL;

#ifdef USE_EPOLL
	int readFD_epoll;
	struct epoll_event *events;
#else
	int nfds;

	fd_set readFD;
	fd_set writeFD;
#endif

	assert ( req != NULL );
	assert ( details != NULL );

	// Malloc the client array after we find out how many clients there are
	while (details != NULL) {
		clients += details->n;
		details = details->next;
	}

	if (clients == 0) {
		fprintf(stderr, "%s:%d Must have more than zero clients!\n", __FILE__, __LINE__ );
		goto cleanup;
	}

#ifndef USE_EPOLL
	if ( clients> FD_SETSIZE ) {
		fprintf(stderr, "%s:%d Client thread can have no more than %d connections\n", __FILE__, __LINE__, FD_SETSIZE );
		goto cleanup;
	}
#endif

	client = calloc(clients, sizeof(*client));

	if ( client == NULL ) {
		fprintf(stderr, "%s:%d calloc error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Blank client before we start
	for ( c = client; c < &client[ clients ]; c++)
		*c = INVALID_SOCKET;

	if ( settings.verbose )
		printf("Core %d: Started client thread\n", req->cores);

	if ( connect_connections(&settings, req, client, &clients_temp) ) {
		goto cleanup;
	}

	if ( clients != clients_temp ) {
		fprintf(stderr, "%s:%d Requested number of clients does not match actual clients (%d != %d)\n", __FILE__, __LINE__, clients, clients_temp );
		goto cleanup;
	}

	buffer = malloc( settings.message_size );

	if ( buffer == NULL ) {
		fprintf(stderr, "%s:%d malloc error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	memset( buffer, (int)BUFFER_FILL, settings.message_size ); // TODO fix 32bit linux compile problem

#ifdef USE_EPOLL
	readFD_epoll = epoll_create(clients);
	if(readFD_epoll == -1) {
		fprintf(stderr, "%s:%d epoll_create() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	events = calloc( clients, sizeof(*events) );
	if ( events == NULL ) {
		fprintf(stderr, "%s:%d calloc error\n", __FILE__, __LINE__ );
		goto cleanup;
	}
#else
	nfds = (int)*client;
	FD_ZERO ( &readFD );
	FD_ZERO ( &writeFD );
#endif

	// Loop all client sockets
	for (c = client; c < &client [ clients ]; c++) {
		SOCKET s = *c;

#ifdef USE_EPOLL
		struct epoll_event event = {0};

		assert ( s != INVALID_SOCKET );

		event.events = EPOLLIN | EPOLLOUT;
		event.data.fd = s;

		if (epoll_ctl(readFD_epoll, EPOLL_CTL_ADD, s, &event) == -1) {
			fprintf(stderr, "%s:%d epoll() error adding client (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			goto cleanup;
		}
#else
		assert ( s != INVALID_SOCKET );

		// Add them to FD sets
		FD_SET( s, &readFD);
		FD_SET( s, &writeFD);

		if ( (int)s >= nfds )
			nfds = (int)s + 1;

		assert ( FD_ISSET(s, &readFD ) );
		assert ( FD_ISSET(s, &writeFD ) );
#endif
	}
	
	 // Signal we are ready
	threads_signal_parent(SIGNAL_READY_TO_GO, settings.threaded_model);
	
	// Wait for the go
	pthread_mutex_lock( &go_mutex );
	while ( bRunning && !bGo ) {
		struct timespec abstime;

		get_timespec_now(&abstime);
		abstime.tv_sec += 1;

		pthread_cond_timedwait( &go_cond, &go_mutex, &abstime);
	}
	pthread_mutex_unlock( &go_mutex );

	next_send_time = get_microseconds();

	// Now start the main loop
	while ( bRunning ) {

#ifdef USE_EPOLL
		int i;
		int ready = epoll_wait(readFD_epoll, events, clients, TRANSFER_TIMEOUT);

		for ( i = 0; i < ready; i++ ) {
			SOCKET s = events[i].data.fd;

			assert ( s != INVALID_SOCKET );

			if (events[i].events & (EPOLLHUP | EPOLLERR)) {
				fprintf(stderr, "%s:%d epoll() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
				closesocket( s );
				continue;
			}

			if( events[i].events & EPOLLIN) {
#else
				struct timeval waittime = {TRANSFER_TIMEOUT / 1000, 0}; // 1 second

				int ret = select(nfds, &readFD, &writeFD, NULL, &waittime);

				if ( ret == SOCKET_ERROR ) {
					fprintf(stderr, "%s:%d select() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
					goto cleanup;
				}

				if ( ret == 0 )
					fprintf(stderr, "%s:%d select() timeout occured\n", __FILE__, __LINE__ );

				// Figure out which sockets have fired
				for (c = client; c < &client [ clients ]; c++ ) {
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
#endif
						int len = recv( s, buffer, settings.message_size, 0);

						if ( len == SOCKET_ERROR ) {
							if ( ERRNO != ECONNRESET ) {
								fprintf(stderr, "%s:%d recv() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
								goto cleanup;
							}
						}

#ifndef USE_EPOLL
						ret--;
#endif
						// The socket has closed
						if ( len <= 0 ) {

							if ( settings.verbose )
								printf("  Client: %d Removed client (%d/%d)\n", req->cores, (int)((c - client) / sizeof(*c)) + 1, clients );

#ifndef USE_EPOLL
							// Quickly check if this client was in the write set
							if ( FD_ISSET( s, &writeFD) ) {
								FD_CLR( s, &writeFD );
								ret--;
							}

							// Unset me from the set
							FD_CLR( s, &readFD );

							// Move this back
							move_down ( c, &client[ clients ] );
							c--;

							// Update the nfds
							nfds = (int)highest_socket(client, clients - 1) + 1;
#endif

							// Invalid this client
							closesocket( s );
							clients--;

							// If this is the last client then just give up!
							if ( clients == 0 )
								goto cleanup;

							// This socket is now closed, so lets go back up to the loop
							continue;
						}
#ifndef USE_EPOLL
					} else {
						// Set the socket on this FD, to save us doing it at the beginning of each loop
						FD_SET( s, &readFD);
					}
					// Check if we are ready to write
					if ( FD_ISSET( s, &writeFD) ) {
						ret--;
#else
					}
					if( events[i].events & EPOLLOUT) {
#endif
						// Rate limiting code
						if ( time_between_sends> 0 ) {
							const unsigned long long now = get_microseconds();

							if ( next_send_time > now )
								continue;

							next_send_time += time_between_sends;
						}

						if ( send( s, buffer, settings.message_size, 0 ) == SOCKET_ERROR ) {
							if ( ERRNO != EWOULDBLOCK && ERRNO != EPIPE ) {
								fprintf(stderr, "%s:%d send() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
								goto cleanup;
							}
						}
#ifdef USE_EPOLL
					}
#endif
#ifndef USE_EPOLL
						} else {
							// Set the socket on this FD, to save us doing it at the beginning of each loop
							FD_SET( s, &writeFD);
						}
					}
					// We have now looped over each socket, If we are here ret MUST be zero
					assert(ret == 0);
				}
#else
			}
		}
#endif

cleanup:

	// Force a stop
	stop_all();

	// Cleanup
	free( buffer );

	// Shutdown client sockets
	if ( client != NULL ) {
		for (c = client; c < &client [ clients ]; c++) {
			SOCKET s = *c;
			if ( s != INVALID_SOCKET ) {
				shutdown ( s, SHUT_RDWR );
				closesocket( s );
			}
		}
	}

#ifdef USE_EPOLL
	close(readFD_epoll);
	free( events );
#endif


	free ( client );

	return NULL;
}
