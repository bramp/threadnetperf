#include "server.h"

#include "global.h"
#include "print.h"
#include "netlib.h"

#include <assert.h>
#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>
//MF: Needed for epoll to work
#include <sys/epoll.h>


#ifndef WIN32
#include <unistd.h>
#endif

// Count of how many threads are not listening
volatile unsigned int server_listen_unready = 0;

/**
	Wait for and accept N connections
	
 * MF: Why don't we create the FD_SET for the incoming connections in this method?
 * and either return it or using pass-by-value?
**/
int accept_connections(const struct server_request *req, SOCKET listen, SOCKET *clients) {

	const struct settings *settings = req->settings;

	unsigned int n = req->n;
	int connected = 0;
	
	//MF: However, we need this additional struct for epoll
	struct epoll_event event;
	
	//MF: Added a new fd set for epoll
		 
	int readFD_epoll = 0;
	fd_set readFD;

	assert ( listen != INVALID_SOCKET );
	assert ( clients != NULL );
	assert ( req->n > 0 );

	//MF: Initalise the FD_SETS
	if(settings->use_epoll) {
		//MF: I'm not sure about this number, but for now let's use n
		readFD_epoll = epoll_create(n);
		//MF: Check the actual return value and change this to a sensible name
		if(readFD_epoll == -1) {
			fprintf(stderr, "%s:%d epoll_create() error %d\n", __FILE__, __LINE__, ERRNO );
			return -1;
		}
		
		//EPOLLHUP and EPOLLERR are included by default.
		event.events = EPOLLIN | EPOLLET;
		//MF: Where should this be?
		event.data.fd = listen;
	
		if (epoll_ctl(readFD_epoll, EPOLL_CTL_ADD, listen, &event) == -1) {
			fprintf(stderr, "%s:%d epoll_ctl() error %d\n", __FILE__, __LINE__, ERRNO );
			return -1;
	    }

	} else {
		FD_ZERO(&readFD);
	}

	// Wait for all connections
	while ( req->bRunning && n > 0 ) {
		struct timeval waittime = {CONTROL_TIMEOUT / 1000, 0};
		int ret;
		struct sockaddr_storage addr;
		socklen_t addr_len = sizeof(addr);
		SOCKET s;
		int send_socket_size, recv_socket_size;

		if(settings->use_epoll) {
			int i=0;
			struct epoll_event events[n];
			if(settings->verbose) {
				printf("  epoll() is being used\n");
			}
			//MF: TODO: Check 1000ms is ok for the timeout
			int num_fds = epoll_wait(readFD_epoll, events, n, 1000);
			for(i=0; i<num_fds; i++) {
			 	if (events[i].events & (EPOLLHUP | EPOLLERR)) {
			 		fprintf(stderr, "%s:%d epoll() error %d\n", __FILE__, __LINE__, ERRNO );
			 		close(events[i].data.fd);
			 		continue;
			 	}

				//We have an incoming connection
			 	if (events[i].data.fd == listen) {
			 		goto accept;
			 	}
			 }
		} else {
		
			FD_SET( listen, &readFD);
	
			if(settings->verbose) {
				printf("  select() is being used\n");
			}
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
			goto accept;
		}
	accept:

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
		//	if ( enable_maxseq ( s , settings->message_size) == SOCKET_ERROR ) {
		//		fprintf(stderr, "%s:%d enable_maxseq() error %d\n", __FILE__, __LINE__, ERRNO );
		//		return 1;
		//	}
		}

#ifndef WIN32
		if ( settings->timestamp ) {
			if ( enable_timestamp(s) == SOCKET_ERROR ) {
				fprintf(stderr, "%s:%d enable_timestamp() error %d\n", __FILE__, __LINE__, ERRNO );
				return 1;
			}
		}
#endif

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
				req->cores, inet_ntoa(((struct sockaddr_in *)&addr)->sin_addr), connected,
				send_socket_size, recv_socket_size );

		n--;
	}

	return 0;
}

#define CLIENT_DISCON -99

/*
 * Perform the recv processing on behave of EITHER select or epoll
 * Return codes
 * 
 * TODO: Make method parameters smaller
 */ 
int recv_processing(const struct settings settings, SOCKET fired_socket, unsigned long long* bytes_recv, 
					unsigned long long* pkt_recv, unsigned long long* pkts_time, 
					unsigned long long* timestamps, unsigned char *buf, int clients,
					struct msghdr msgs, size_t msg_control_len) {
	int len;

#ifdef WIN32
	len = recv( fired_socket, buf, settings.message_size, 0 );
#else
	msgs.msg_controllen = msg_control_len;
	len = recvmsg( fired_socket, &msgs, 0);
#endif
	// The socket has closed (or an error has occured)
	if ( len <= 0 ) {		
			if ( len == SOCKET_ERROR ) {
				int lastErr = ERRNO;
				
				// If it is a blocking error just continue
				if ( lastErr == EWOULDBLOCK ) {
					//IGNORE THIS ERROR?
				}
	
				else if ( lastErr != ECONNRESET ) {
					fprintf(stderr, "%s:%d recv() error %d\n", __FILE__, __LINE__, lastErr );
					if ( settings.verbose ) printf("  Server: %d Removed client (%d/%d)\n", settings.servercores, fired_socket+1, clients );
					return CLIENT_DISCON;					
				}
				if ( settings.verbose ) printf("  Server: %d Removed client (%d/%d)\n", settings.servercores, fired_socket+1, clients );
				return CLIENT_DISCON;
			}
			
		}
	
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
					timestamps ++;
					pkts_time += now - ns;
				} else {
					if ( ns != 0 )
						fprintf(stderr, "%s:%d Invalid timestamp %llu > %llu\n", __FILE__, __LINE__, ns, now );
				}
	
				#ifdef CHECK_TIMES
					if(pkts_recv [ i ] < CHECK_TIMES ) {
						req->stats.processed_something = 1;
						req->stats.processing_times[pkts_recv [ i ]] = t;
					}
				#endif
			}
			cmsg = CMSG_NXTHDR(&msgs, cmsg);
		}
	}
#endif
	// Count how many bytes have been received
	*bytes_recv += len;
	*pkt_recv  = *pkt_recv + 1;
	
	return 0;
}

/**
	Creates a server, and handles each incoming client
	MF: Added a local variable to represent the max set size
	MF: TODO: Remove this local variable ?
*/
void *server_thread(void *data) {
	struct server_request * const req = data;

	// Copy the global settings
	const struct settings settings = *req->settings;

	SOCKET s = INVALID_SOCKET; // The listen server socket
	int max_set = 0;
	
	//MF: This is to remove the need for a limit when using epoll
	if(settings.use_epoll)
		max_set = 1024;
	else 
		max_set = FD_SETSIZE;
	
	SOCKET client [ max_set ];
	SOCKET *c = client;

	
	int clients = req->n; // The number of clients

	int return_stats = 0; // Should we return the stats?

	unsigned int i;

	// TODO WHY are all these counted per socket? why not a aggrigate?
	unsigned long long bytes_recv [ max_set ]; // Bytes received from each socket
	unsigned long long pkts_recv  [ max_set ]; // Number of recv calls from each socket

	unsigned long long pkts_time  [ max_set ]; // Total time packets spent (in network) for each socket (used in timestamping)
	unsigned long long timestamps [ max_set ]; // Number of timestamps received

	unsigned char *buf = NULL;
#ifndef WIN32
	struct msghdr msgs = {0};
	struct iovec msg_iov = {NULL, 0}; // Buffer to read data into, will be malloced later
	size_t msg_control_len = 1024;
#endif

	struct sockaddr_in addr; // Address to listen on

	struct timespec waittime = {0, 100000000}; // 100 milliseconds

	long long start_time; // The time we started
	long long end_time; // The time we ended

	int send_socket_size, recv_socket_size; // The socket buffer sizes

	//MF: added the "fd_set" for epoll
	int		readFD_epoll;
	fd_set 	readFD;
	
	//MF: Poll event struct  
	struct epoll_event event;
	
	int one = 1;

	int nfds;

	if ( settings.verbose )
		printf("Core %d: Started server thread port %d\n", req->cores, req->port );

	// Blank client before we start
	for ( c = client; c < &client[ sizeof(client) / sizeof(*client) ]; c++)
		*c = INVALID_SOCKET;

	memset( bytes_recv, 0, sizeof(bytes_recv) );
	memset( pkts_recv, 0, sizeof(pkts_recv) );
	memset( pkts_time, 0, sizeof(pkts_time) );
	memset( timestamps, 0, sizeof(timestamps) );

	if ( req->n > sizeof(client) / sizeof(*client) ) {
		fprintf(stderr, "%s:%d server_thread() error Server thread can have no more than %d connections (%d specified)\n", __FILE__, __LINE__, (int)(sizeof(client) / sizeof(*client)), req->n );
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
//		if ( enable_maxseq ( s , settings.message_size) == SOCKET_ERROR ) {
//			fprintf(stderr, "%s:%d enable_maxseq() error %d\n", __FILE__, __LINE__, ERRNO );
//			goto cleanup;
//		}
	}

#ifdef WIN32
	if( settings.use_epoll ) {		
			fprintf(stderr, "%s:%d error epoll() not available on windows %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
	}
#endif
#ifndef WIN32
	
	
	if ( settings.timestamp  ) {
		if ( enable_timestamp(s) == SOCKET_ERROR ) {
			fprintf(stderr, "%s:%d enable_timestamp() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}
	}
#endif

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
	buf = malloc( settings.message_size );
	if ( buf == NULL ) {
		fprintf(stderr, "%s:%d malloc() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

#ifndef WIN32
	msg_iov.iov_len = settings.message_size;
	msg_iov.iov_base = buf;

	msgs.msg_name = NULL;
	msgs.msg_namelen = 0;
	msgs.msg_iov = &msg_iov;
	msgs.msg_iovlen = 1;
	msgs.msg_flags = 0;

	// If we need the control messages we should set them up
	if ( settings.timestamp ) {
		msg_control_len = 1024;
		msgs.msg_control = malloc( msg_control_len );
		if ( msgs.msg_control == NULL ) {
			fprintf(stderr, "%s:%d malloc() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}
	} else {
		msg_control_len = 0;
	}
#endif
	if(settings.use_epoll) {
		readFD_epoll = epoll_create(clients);
		if(readFD_epoll == -1) {
			fprintf(stderr, "%s:%d epoll_create() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}
	}
	else {
		// Setup FD_SETs
		FD_ZERO( &readFD );
	}
	nfds = (int)*client;

	// Add all the client sockets to the fd_set
	for (c = client ; c < &client [clients] ; c++) {
		assert ( *c != INVALID_SOCKET );

		//MF: If we are using epoll let's add them to that "fd_set"
		if(settings.use_epoll) { 
			//MF: TODO: Check that the client's socket is none-blocking
			//MF: TODO: Check that we don't want or need the servers socket here!
			disable_blocking(*c);
			
			/*
			 * MF: THIS IS THE PROBLEM CODE!
			 * 
			 * | EPOLLET
			 */ 
			
			event.events = EPOLLIN ;
			event.data.fd = *c;
			if (epoll_ctl(readFD_epoll, EPOLL_CTL_ADD, *c, &event) == -1) {
				fprintf(stderr, "%s:%d epoll() error adding server %d\n", __FILE__, __LINE__, ERRNO );
				goto cleanup;
			}
		} else {
			FD_SET( *c, &readFD);
			if ( (int)*c > nfds )
				nfds = (int)*c;
		}
	}
	nfds = nfds + 1;
	
	//At this point we've populated the fd_set we need for either select() or epoll() 

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
		
		if(!settings.use_epoll) {
			ret = select( nfds, &readFD, NULL, NULL, &waittime);
			
			// Figure out which sockets have fired
			for (c = client, i = 0 ; c < &client [ clients ]; c++ ) {
				
				if ( ret ==  SOCKET_ERROR ) {
					fprintf(stderr, "%s:%d select() error %d\n", __FILE__, __LINE__, ERRNO );
					goto cleanup;
				}
				#ifdef _DEBUG
				if ( ret == 0 && !req->bRunning )
					fprintf(stderr, "%s:%d select() timeout occured\n", __FILE__, __LINE__ );
				#endif
				
				SOCKET s = *c;

				assert ( s != INVALID_SOCKET );

				if ( ret == 0 ) {
					FD_SET( s, &readFD);
					continue;
				}
				if ( FD_ISSET( s, &readFD) ) {
					//recv_processing//
					
					int ret_recv_process = recv_processing(settings, s, &bytes_recv[i], &pkts_recv[i], &pkts_time[i], &pkts_time[i], buf ,clients,msgs ,msg_control_len);
					ret--;
					if(ret_recv_process < 0) {
						if(ret_recv_process == CLIENT_DISCON) {
							//TODO: Add the code to change the select fd set stuff
							//TODO: WARN: Duplicate code 
							clients--;
							// If this is the last client then just give up!
							if ( clients == 0 )
								goto end_loop;

							// Update the nfds
							nfds = (int)highest_socket(client, clients) + 1;

							continue;
						}
					}
				} else {
					// Set the socket on this FD, to save us doing it at the beginning of each loop
					FD_SET( s, &readFD);
				}

				i++;
			}
		} else {
			struct epoll_event events[clients];
		//	fprintf(stderr, "MF: num clients %d on line %d\n", clients, __LINE__);
			//num_fds are the number of sockets that have some action
			int num_fds = epoll_wait(readFD_epoll, events, clients, 1000);
			
			//fprintf(stderr, "MF: num_fds fired %d on line %d\n", num_fds, __LINE__);
			for(i=0; i<num_fds; i++) {
				int ret_recv_process=0;
				if (events[i].events & (EPOLLHUP | EPOLLERR)) {
					fprintf(stderr, "%s:%d epoll() error %d\n", __FILE__, __LINE__, ERRNO );
					//Closing a file descriptor automagically removes it from the epoll fd set.
					close(events[i].data.fd);
					continue;
				}
				
			/*	//Socket with action is our listening socket, therefore we have a new connection
				if(events[i].data.fd == s) {
					//For now i'm just going to ignore any more incoming connections!
					continue;
				} 
			*/
		
				ret_recv_process = recv_processing(settings, events[i].data.fd, &bytes_recv[i], &pkts_recv[i], &pkts_time[i], &pkts_time[i], buf ,clients, msgs, msg_control_len);
				
				if(ret_recv_process < 0 ) {
					if(ret_recv_process == CLIENT_DISCON) {
						if ( settings.verbose )
						printf("  Server: %d Removed client (%d/%d)\n", settings.servercores, events[i].data.fd+1, clients );
						close(events[i].data.fd);
						//TODO: WARN: Duplicate code 
						clients--;
						// If this is the last client then just give up!
						if ( clients == 0 )
							goto end_loop;

						// Update the nfds
						nfds = (int)highest_socket(client, clients) + 1;

						continue;
					}	
				}
			}
		}
	}
end_loop:

	// We have finished, work out some stats
	end_time = get_microseconds();
	
	// Add up all the client bytes
	req->stats.cores = req->cores;
	req->stats.duration = end_time - start_time;
	req->stats.bytes_received = 0;
	req->stats.pkts_received = 0;
	for (i = 0 ; i <  sizeof(bytes_recv) / sizeof(*bytes_recv); i++) {
		req->stats.bytes_received += bytes_recv [ i ];
		req->stats.pkts_received += pkts_recv [ i ];
		req->stats.pkts_time += pkts_time [ i ];
		req->stats.timestamps += timestamps [ i ];
	}
	return_stats = 1;

cleanup:
	// Force a stop
	stop_all();

	// Cleanup
	if ( buf )
		free( buf );

#ifndef WIN32
	if ( msgs.msg_control )
		free ( msgs.msg_control );
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
		}
	}

	if ( return_stats )
		return &req->stats;

	return NULL;
}
