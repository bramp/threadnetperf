#include "server.h"

#include "global.h"
#include "print.h"
#include "netlib.h"

#include "serialise.h" // To allow us to send the results via the IPC socket.

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

#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))

const unsigned int max_cores; // The number of CPU cores this machine has

/**
	Wait for and accept N connections
**/
int accept_connections(const struct server_request *req, SOCKET listen, SOCKET *clients) {

	const struct settings *settings = req->settings;

	unsigned int n = req->n;
	int connected = 0;

	fd_set readFD;

#ifdef MF_FLIPPAGE
	int flippage = 1;
#endif

#ifdef MF_NOCOPY
	int nocopy = 1;
#endif

	assert ( listen != INVALID_SOCKET );
	assert ( clients != NULL );
	assert ( req->n > 0 );

	FD_ZERO(&readFD);
	

	// Wait for all connections
	while ( bRunning && n > 0 ) {
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
				fprintf(stderr, "%s:%d select() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
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
			fprintf(stderr, "%s:%d accept() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			return 1;
		}

		send_socket_size = set_socket_send_buffer( s, settings->socket_size );
		if ( send_socket_size < 0 ) {
			fprintf(stderr, "%s:%d set_socket_send_buffer() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			return 1;
		}

		recv_socket_size = set_socket_recv_buffer( s, settings->socket_size );
		if ( send_socket_size < 0 ) {
			fprintf(stderr, "%s:%d set_socket_recv_buffer() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			return 1;
		}

		if ( settings->disable_nagles ) {
			if ( disable_nagle( s ) == SOCKET_ERROR ) {
				fprintf(stderr, "%s:%d disable_nagle() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
				return 1;
			}
		//	if ( enable_maxseq ( s , settings->message_size) == SOCKET_ERROR ) {
		//		fprintf(stderr, "%s:%d enable_maxseq() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		//		return 1;
		//	}
		}

#ifndef WIN32
		if ( settings->timestamp ) {
			if ( enable_timestamp(s) == SOCKET_ERROR ) {
				fprintf(stderr, "%s:%d enable_timestamp() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
				return 1;
			}
		}
#endif

		// Always disable blocking (to work around linux bug)
		if ( disable_blocking(s) == SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d disable_blocking() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			return 1;
		}

#ifdef MF_FLIPPAGE
		// Turn on the flippage socket option
		// TODO: MF: Fix the "99" - it should be SOCK_FLIPPAGE
		if ( setsockopt(s, SOL_SOCKET, 99, &flippage, sizeof(flippage)) == SOCKET_ERROR) {
			fprintf(stderr, "%s:%d set_socktopt() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			return 1;

		}
#endif

#ifdef MF_NOCOPY
		if ( setsockopt(s, SOL_SOCKET, 98, &nocopy, sizeof(nocopy)) == SOCKET_ERROR) {
			fprintf(stderr, "%s:%d set_socktopt() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			return 1;
		}
#endif

		assert ( *clients == INVALID_SOCKET );
		*clients = s;
		clients++;
		connected++;

		if ( settings->verbose )
			printf("  Server: %d incoming client %s (%d) socket size: %d/%d\n",
				req->cores, inet_ntoa(((struct sockaddr_in *)&addr)->sin_addr), connected,
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

	int clients = req->n; // The number of clients

	SOCKET *client = NULL;
	SOCKET *c;

	int return_stats = 0; // Should we return the stats?

	// We count stats per socket, so we can get more fine grain stats
	unsigned long long bytes_recv = 0; // Bytes received from each socket
	unsigned long long pkts_recv  = 0;  // Number of recv calls from each socket

	unsigned long long pkts_time  = 0;  // Total time packets spent (in network) for each socket (used in timestamping)
	unsigned long long timestamps = 0; // Number of timestamps received

	unsigned char *buf = NULL;

#ifndef WIN32
	struct msghdr msgs = {0};
	struct iovec msg_iov = {NULL, 0}; // Buffer to read data into, will be malloced later
	size_t msg_control_len = 1024; // TODO what does 1024 mean?

#ifdef USE_EPOLL
	int		readFD_epoll = 0;
	struct epoll_event *events = NULL;
#endif

#endif

#ifndef USE_EPOLL
	fd_set readFD;
	int nfds = 0;
#endif


	int page_size, num_pages;
#ifdef MF_FLIPPAGE
	int flippage = 1;
#endif

	struct sockaddr_in addr; // Address to listen on

	long long start_time; // The time we started
	long long end_time; // The time we ended

	int send_socket_size, recv_socket_size; // The socket buffer sizes

	unsigned int i = 0;
	const int one = 1;
	int recv_size = 0;

	if ( settings.verbose )
		printf("Core %d: Started server thread port %d from pid (%d)\n", req->cores, req->port , getpid());

	// Malloc client space for many of the arrays
	client = calloc(clients, sizeof(*client));

	if ( client == NULL ) {
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Blank client before we start
	for ( c = client; c < &client[clients]; c++)
		*c = INVALID_SOCKET;

	// Create the listen socket
	s = socket( PF_INET, settings.type, settings.protocol);

	if ( s == INVALID_SOCKET ) {
		fprintf(stderr, "%s:%d socket() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	send_socket_size = set_socket_send_buffer( s, settings.socket_size );
	if ( send_socket_size < 0 ) {
		fprintf(stderr, "%s:%d set_socket_send_buffer() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	recv_socket_size = set_socket_recv_buffer( s, settings.socket_size );
	if ( recv_socket_size < 0 ) {
		fprintf(stderr, "%s:%d set_socket_recv_buffer() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	if ( settings.disable_nagles ) {
		if ( disable_nagle( s ) == SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d disable_nagle() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			goto cleanup;
		}
//		if ( enable_maxseq ( s , settings.message_size) == SOCKET_ERROR ) {
//			fprintf(stderr, "%s:%d enable_maxseq() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
//			goto cleanup;
//		}
	}

#ifndef WIN32
	if ( settings.timestamp  ) {
		if ( enable_timestamp(s) == SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d enable_timestamp() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			goto cleanup;
		}
	}
#endif

	// SO_REUSEADDR
	if ( setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d setsockopt(SOL_SOCKET, SO_REUSEADDR) error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( req->port );
	
	// Bind
	if ( bind( s, (struct sockaddr *) &addr, sizeof(addr)) == SOCKET_ERROR) {
		fprintf(stderr, "%s:%d bind() error (%d) %s from pid (%d)\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO), getpid() );
		goto cleanup;
	}

	// Listen
	if ( (settings.type == SOCK_STREAM || settings.type==SOCK_SEQPACKET) ) {
		if ( listen(s, SOMAXCONN) == SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d listen() error (%d) %s from pid (%d)\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO), getpid() );
			goto cleanup;
		}
	}
	// We are now listening and waiting
	threads_signal_parent ( SIGNAL_READY_TO_ACCEPT, settings.threaded_model );

	// If this is a STREAM then accept each connection
	if ( settings.type == SOCK_STREAM ) {
		// Wait until all connections have been accepted
		if ( accept_connections(req, s, client) ) {
			goto cleanup;
		}

	// If this is a DGRAM, then we don't have a connection per client, but instead one server socket
	} else if ( settings.type == SOCK_DGRAM ) {
#ifdef MF_FLIPPAGE
		// Turn on the flippage socket option
		// TODO: MF: Fix the "99" - it should be SOCK_FLIPPAGE
		if ( setsockopt(s, SOL_SOCKET, 99, &flippage, sizeof(flippage)) == SOCKET_ERROR) {
			fprintf(stderr, "%s:%d set_socktopt() error (%d) %s\n",__FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			goto cleanup;
		 }
#endif

		*client = s;
		clients = 1;
	}

	// By this point all the clients have connected, but the test hasn't started yet

	// Setup the buffer
	page_size = getpagesize();
	num_pages = roundup(settings.message_size, page_size);
	if( settings.verbose )
		printf("vallocing of buffer of %d bytes\n", num_pages);
	buf = valloc( num_pages );

	recv_size = num_pages;

	if ( buf == NULL ) {
		fprintf(stderr, "%s:%d malloc() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

#ifndef WIN32
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
#endif

	msg_iov.iov_len = recv_size;
	msg_iov.iov_base = buf;

	msgs.msg_name = NULL;
	msgs.msg_namelen = 0;
	msgs.msg_iov = &msg_iov;
	msgs.msg_iovlen = 1;
	msgs.msg_flags = 0;

	// If we need the control messages we should set them up
	if ( settings.timestamp ) {
		msgs.msg_control = malloc( msg_control_len );
		if ( msgs.msg_control == NULL ) {
			fprintf(stderr, "%s:%d malloc() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			goto cleanup;
		}
		msg_control_len = 1024;
	} else {
		msgs.msg_control = NULL;
		msg_control_len = 0;
	}
#endif

#ifndef USE_EPOLL
	// Setup FD_SETs
	FD_ZERO( &readFD );
	nfds = (int)*client;
#endif

	// Add all the client sockets to the fd_set
	for (c = client ; c < &client [clients] ; c++) {
#ifdef USE_EPOLL
		struct epoll_event event = {0};

		event.events = EPOLLIN ;
		event.data.fd = *c;
		
		assert ( *c != INVALID_SOCKET );

		if (epoll_ctl(readFD_epoll, EPOLL_CTL_ADD, *c, &event) == -1) {
			fprintf(stderr, "%s:%d epoll() error adding server (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			goto cleanup;
		}
#else
		assert ( *c != INVALID_SOCKET );

		FD_SET( *c, &readFD);
		if ( (int)*c >= nfds )
			nfds = (int)*c + 1;
#endif
	}

	 // Signal we are ready
	threads_signal_parent( SIGNAL_READY_TO_GO, settings.threaded_model );

	// Wait for the go
//	pthread_mutex_lock( &go_mutex );
	while ( bRunning && !bGo ) {
//		struct timespec abstime;

//		get_timespec_now(&abstime);
//		abstime.tv_sec += 1;

//		pthread_cond_timedwait( &go_cond, &go_mutex, &abstime);
	}
//	pthread_mutex_unlock( &go_mutex );

	// Start timing
	start_time = get_microseconds();
	
	while ( bRunning ) {
		int ret, len;

#ifdef USE_EPOLL
		
		ret = epoll_wait(readFD_epoll, events, clients, TRANSFER_TIMEOUT);
		
		if ( ret < 0 ) {
			//fprintf(stderr, "%s:%d epoll_wait() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			ret = 0;
		}

		//fprintf(stderr, "MF: num_fds fired %d on line %d\n", num_fds, __LINE__);
		for ( i = 0; i < ret; i++ ) {
			SOCKET s = events[i].data.fd;	
			assert ( s != INVALID_SOCKET );
			if (events[i].events & (EPOLLHUP | EPOLLERR)) {
				fprintf(stderr, "%s:%d epoll() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
				//Closing a file descriptor automagically removes it from the epoll fd set.
				closesocket( s );
				continue;
			}
#else
		struct timeval waittime = {TRANSFER_TIMEOUT / 1000, 0}; // 1 second
		ret = select( nfds, &readFD, NULL, NULL, &waittime);

		if ( ret == 0 && !bRunning )
			fprintf(stderr, "%s:%d select() timeout occured\n", __FILE__, __LINE__ );

		// Figure out which sockets have fired
		for (c = client; c < &client [ clients ]; c++ ) {
			SOCKET s = *c;

			assert ( s != INVALID_SOCKET );

			if ( ret == 0 ) {
				FD_SET( s, &readFD);
				continue;
			}

			if ( FD_ISSET( s, &readFD) ) {
				ret--;
#endif

#ifdef WIN32
				len = recv(s, buf, recv_size, 0);
#else
				msgs.msg_controllen = msg_control_len;
				len = recvmsg(s, &msgs, 0);
#endif

				// The socket has closed (or an error has occured)
				if ( len <= 0 ) {
					if ( len == SOCKET_ERROR ) {
						int lastErr = ERRNO;

						// If it is a blocking error just continue
						if ( lastErr == EWOULDBLOCK )
							continue;
						
						else if ( lastErr == EINTR ) {
							fprintf( stderr,"%s:%d recv(%d) interrupted by singal\n", __FILE__, __LINE__, s);
							continue;
						}

						else if ( lastErr != ECONNRESET ) {
							fprintf(stderr, "%s:%d recv(%d) error (%d) %s\n", __FILE__, __LINE__, s, lastErr, strerror(lastErr) );
							printf("(%d) has %d clients left\n", getpid(), clients);
							goto cleanup;
						} 
					}

					if ( settings.verbose )
						printf("  Server: %d Removed client (%d/%d)\n", req->cores, i + 1, clients );

					// Invalidate this client
					closesocket( s );
					

#ifndef USE_EPOLL
					// Move back
					move_down ( c, &client[ clients ] );
					c--;
#endif
					clients--;

					// If this is the last client then just give up!
					if ( clients == 0 )
						goto end_loop;

#ifndef USE_EPOLL
					// Update the nfds
					FD_CLR( s, &readFD );
					nfds = (int)highest_socket(client, clients) + 1;
#endif
					continue;

				} else {
					// We could dirty the buffer
					if (settings.dirty) {
						// These is volatile to stop the compiler removing this loop
						volatile int *d;
						volatile int temp = 0;
						for (d=(int *)buf; d<(int *)(buf + len); d++) {
							temp += *d;
						}
					}

#ifndef WIN32
					if ( settings.timestamp ) {
						const unsigned long long now = get_nanoseconds();

						struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msgs);
						while ( cmsg != NULL) {

							if ( cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_TIMESTAMPNS ) {
								const struct timespec *ts = (struct timespec *) CMSG_DATA( cmsg );
								const unsigned long long ns = ts->tv_sec * 1000000000 + ts->tv_nsec;

								if(ns <= now) {
									timestamps++;
									pkts_time += now - ns;
								} else {
									if ( ns != 0 )
										fprintf(stderr, "%s:%d Invalid timestamp %llu > %llu\n", __FILE__, __LINE__, ns, now );
								}

								#ifdef CHECK_TIMES
									if(pkts_recv < CHECK_TIMES ) {
										req->stats.processed_something = 1;
										req->stats.processing_times[pkts_recv] = t;
									}
								#endif
							}
							cmsg = CMSG_NXTHDR(&msgs, cmsg);
						}
					}
#endif
					// Count how many bytes have been received
					bytes_recv += len;
					pkts_recv++;
				}

#ifndef USE_EPOLL
			} else {
				// Set the socket on this FD, to save us doing it at the beginning of each loop
				FD_SET( s, &readFD);
			}
#endif
		}
	}
end_loop:

	// We have finished, work out some stats
	end_time = get_microseconds();

	// Add up all the client bytes
	req->stats.cores = req->cores;
	req->stats.duration = end_time - start_time;
	req->stats.bytes_received = 0;
	req->stats.pkts_received  = 0;

	req->stats.bytes_received += bytes_recv;
	req->stats.pkts_received  += pkts_recv;
	req->stats.pkts_time      += pkts_time;
	req->stats.timestamps     += timestamps;

	return_stats = 1;

	
cleanup:
	
	// Force a stop
	stop_all(settings.threaded_model);


	// Cleanup
	free( buf );

#ifndef WIN32
#ifdef USE_EPOLL
	close(readFD_epoll);
	free( events );
#else
	if ( msgs.msg_control )
		free ( msgs.msg_control );
#endif
#endif

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
			*c = INVALID_SOCKET;
		}
	}

	if ( client )
		free ( client );

	if ( return_stats )
		send_stats_from_thread(req->stats);	
	else 
		printf("(%d) is skipping sending of stats\n", getpid());
	return NULL;
}
