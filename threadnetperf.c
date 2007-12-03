
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <malloc.h>

#include <time.h>

#ifdef WIN32
	#define WIN32_LEAN_AND_MEAN

	#include "winsock2.h"
	#include "Ws2tcpip.h"

	#define ERRNO (WSAGetLastError())
	#define ECONNRESET WSAECONNRESET

	#define SHUT_RDWR SD_BOTH

	// Define some dummy structs, currently they do nothing
	typedef struct {
		unsigned long int __cpu_mask;
	} cpu_set_t;

	/* Access functions for CPU masks.  */
	#define CPU_ZERO(cpusetp)
	#define CPU_SET(cpu, cpusetp)
	#define CPU_CLR(cpu, cpusetp)
	#define CPU_ISSET(cpu, cpusetp)

#else

	#include <errno.h>
	#include <sys/time.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <unistd.h>
	#include <arpa/inet.h> // For inet_addr

	#define ERRNO errno
	#define closesocket(s) close(s)

	#ifndef SOCKET
		#define SOCKET int
		#define INVALID_SOCKET (-1)
		#define SOCKET_ERROR (-1)
	#endif
#endif

#define _GNU_SOURCE 
#include <pthread.h> // We assume we have a pthread library (even on windows)
#include <sched.h>


// Flag to indidcate if we are still running
int bRunning = 1;

// The message size
int message_size = 65535;

// How long (in seconds) this should run for
int duration = 10;

struct server_request {
	unsigned short port;
};

// Struct to pass to a client thread
struct client_request {
	struct sockaddr_in addr;
	//int addr_len;
	unsigned int n;
};

// Returns the number of microseconds since a epoch
long long get_microseconds() {
	long long microseconds = 0;

#ifdef WIN32
	FILETIME ft;

	GetSystemTimeAsFileTime(&ft);

	microseconds |= ft.dwHighDateTime;
	microseconds <<= 32;
	microseconds |= ft.dwLowDateTime;

	microseconds /= 10;	//convert into microseconds
#else
	struct timeval tv;

	gettimeofday(&tv, NULL); 
	microseconds = tv.tv_sec * 1000000 + tv.tv_usec;

#endif

	return microseconds;
}

// Move all the elements after arr down one
void move_down ( SOCKET *arr, SOCKET *arr_end ) {

	// Check this socket isn't already invalid
	assert ( *arr != INVALID_SOCKET );

	*arr = INVALID_SOCKET;

	// Move any other clients down
	while ( (arr + 1) < arr_end ) {
		*arr = *(arr + 1);
		arr++;

		// Check we didn't just copy a INVALID_SOCKET
		assert ( *arr != INVALID_SOCKET );
	}
}

#ifndef pthread_attr_setaffinity_np
	int pthread_attr_setaffinity_np ( pthread_attr_t *attr, size_t cpusetsize, const cpu_set_t *cpuset) {
		return 0;
	}
#endif

/**
	Create a thread on a specific core(s)
*/
int pthread_create_on( pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset) {

	pthread_attr_t thread_attr;
	int ret;

	if (attr == NULL) {
		pthread_attr_init ( &thread_attr );
		attr = &thread_attr;
	}

	// Set the CPU
	ret = pthread_attr_setaffinity_np( attr, cpusetsize, cpuset );
	if (ret)
		goto cleanup;

	ret = pthread_create(thread, attr, start_routine, arg);

cleanup:
	if ( attr == &thread_attr )
		pthread_attr_destroy ( &thread_attr );

	return ret;
}


/**
	Creates a server, and handles each incoming client
*/
void *server_thread(void *data) {
	const struct server_request *req = data;

	SOCKET s = INVALID_SOCKET; // The listen server socket

	SOCKET client [ FD_SETSIZE - 1 ]; // We can only have 1 server socket, and (FD_SETSIZE - 1) clients
	SOCKET *c = client;
	int clients = 0; // The number of clients
	
	int i;
	long long bytes_recv [ FD_SETSIZE - 1 ];
	long long total_bytes_recv;

	char *buffer = NULL; // Buffer to read data into, will be malloced later
	struct sockaddr_in addr; // Address to listen on

	long long start_time; // The time we started
	long long end_time; // The time we ended

	// Blank client before we start
	for ( c = client; c < &client[ sizeof(client) / sizeof(*client) ]; c++)
		*c = INVALID_SOCKET;

	//s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	s = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if ( s == INVALID_SOCKET ) {
		fprintf(stderr, "%s: %d socket() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( req->port );

	if ( bind( s, (struct sockaddr *) &addr, sizeof(addr)) == SOCKET_ERROR) {
		fprintf(stderr, "%s: %d bind() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( listen(s, SOMAXCONN) == SOCKET_ERROR ) {
		fprintf(stderr, "%s: %d listen() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	// Setup the buffer
	buffer = malloc( message_size );
	if ( buffer == NULL ) {
		fprintf(stderr, "%s: %d malloc() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	// Start timing
	start_time = get_microseconds();

	while ( bRunning ) {
		fd_set readFD;
		struct timeval waittime = {0, 100}; // 100 microseconds
		int ret;

		FD_ZERO( &readFD );

		// Add the listen socket (only if we have room for more clients)
		if ( clients < sizeof(client) / sizeof(*client) )
			FD_SET(s, &readFD);

		// Add all the client sockets
		for (c = client ; c < &client [ clients] ; c++) {
			assert ( *c != INVALID_SOCKET );

			FD_SET( *c, &readFD);
		}

		ret = select(0, &readFD, NULL, NULL, &waittime);
		if ( ret ==  SOCKET_ERROR ) {
			fprintf(stderr, "%s: %d select() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		// Did the listen socket fire?
		if ( FD_ISSET(s, &readFD) ) {
			struct sockaddr_storage addr;
			socklen_t addr_len = sizeof(addr);

			// Accept a new client socket
			SOCKET c = accept( s, (struct sockaddr *)&addr, &addr_len );

			if ( c == INVALID_SOCKET ) {
				fprintf(stderr, "%s: %d accept() error %d\n", __FILE__, __LINE__, ERRNO );
				goto cleanup;
			}

			assert ( client[ clients ] == INVALID_SOCKET );
			client[ clients ] = c;
			bytes_recv [ clients ] = 0;
			clients++;

			ret--;
		}

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
					continue;

				} else {
					// We could dirty the buffer

					// Count how many bytes have been received
					bytes_recv [ i ] += len;
				}
			}

			// Move the socket on (if needed)
			i++;
		}
	}

	// We have finished, work out some stats
	end_time = get_microseconds();

	// Add up all the client bytes
	total_bytes_recv = 0;
	for (i = 0 ; i < clients ; i++) {
		assert ( client[i] != INVALID_SOCKET );

		total_bytes_recv += bytes_recv [ i ];
	}

	printf( "Received %lld Mbytes/s\n", (total_bytes_recv / (end_time-start_time)));

cleanup:
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

	// Now start the main loop
	while ( bRunning ) {
		fd_set readFD;
		fd_set writeFD;
		int ret;
		struct timeval waittime = {0, 100}; // 100 microseconds

		FD_ZERO ( &readFD ); FD_ZERO ( &writeFD );

		// Loop all client sockets
		for (c = client ; c < &client [ clients ] ; c++) {
			s = *c;
			assert ( s != INVALID_SOCKET );

			// Add them to FD sets
			FD_SET( s, &readFD);
			FD_SET( s, &writeFD);
		}

		ret = select(0, &readFD, &writeFD, NULL, &waittime);
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

			// Move the socket on (if needed)
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

#ifdef WIN32
/**
	Function to setup the winsock libs
*/
void setup_winsock() {
	WSADATA wsaData;
	
	if ( WSAStartup(MAKEWORD(2,2), &wsaData) ) {
		fprintf(stderr, "%s: %d WSAStartup() error\n", __FILE__, __LINE__ );
		return;
	}
}

void cleanup_winsock() {
	WSACleanup();
}
#endif

#ifdef WIN32
int usleep(unsigned int useconds) {
	struct timespec waittime;
	
	waittime.tv_sec = 0;
	waittime.tv_nsec = useconds * 1000; 

	if ( useconds > 1000000 )
		return EINVAL;

	pthread_delay_np ( &waittime );
	return 0;
}
#endif

/**
	Wait until duration has passed
*/
void pause_for_duration(unsigned int duration) {
	long long start_time; // The time we started

	// Make sure duration is in microseconds
	duration = duration * 1000000;

	// This main thread controls when the test ends
	start_time = get_microseconds();

	while ( bRunning ) {
		long long now = get_microseconds();

		if ( now - start_time > duration ) {
			bRunning = 0;
			break;
		}

		usleep( 100000 );
	}
}

int main (int argc, const char *argv[]) {
	struct server_request sreq;
	struct client_request creq;
	pthread_t *thread; // Array to handle thread handles
	unsigned int threads; // Total number of threads
	unsigned int i;
	cpu_set_t cpus;
	int ret;
	

#ifdef WIN32
	setup_winsock();
#endif

	threads = 2;
	thread = malloc( threads * sizeof(*thread) );
	memset (thread, 0, threads * sizeof(*thread));

	// Create all the threads
	CPU_ZERO ( &cpus );
	CPU_SET ( 0, &cpus );

	// Create all the server threads
	sreq.port = 1234;
	ret = pthread_create_on( &thread[0], NULL, server_thread, &sreq, sizeof(cpus), &cpus);
	if ( ret ) {
		fprintf(stderr, "%s: %d pthread_create_on() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Create all the client threads
	creq.addr.sin_family = AF_INET;
	creq.addr.sin_addr.s_addr = inet_addr( "127.0.0.1" );
	creq.addr.sin_port = htons( 1234 );
	creq.n = 1;

	ret = pthread_create_on( &thread[1], NULL, client_thread, &creq, sizeof(cpus), &cpus);
	if ( ret ) {
		fprintf(stderr, "%s: %d pthread_create_on() error\n", __FILE__, __LINE__ );
		goto cleanup;
	}

	// Now wait unti the test is completed
	pause_for_duration( duration );

cleanup:

	bRunning = 0;

	// Block waiting until all threads die
	for (i = 0; i < threads; i++) {
		assert ( thread [i] != 0 );
		pthread_join( thread[i], NULL );
	}

	free ( thread );

#ifdef WIN32
	cleanup_winsock();
#endif

	return 0;
}
