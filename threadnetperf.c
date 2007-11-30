
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <malloc.h>

#include <pthread.h> // We assume we have a pthread library (even on windows)
#include <sched.h>

#ifdef WIN32
	#define WIN32_LEAN_AND_MEAN
	#include "winsock2.h"
	#define ERRNO (WSAGetLastError())
	#define ECONNRESET WSAECONNRESET

	#define SHUT_RDWR SD_BOTH

#else

	#include <errno.h>
	#include <sys/time.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <unistd.h>

	#define ERRNO errno
	#define closesocket(s) close(s)

	#ifndef IPPROTO_TCP
	#define IPPROTO_TCP 6
	#endif

	#ifndef SOCKET
		#define SOCKET int
		#define INVALID_SOCKET (-1)
		#define SOCKET_ERROR (-1)
	#endif
#endif

// Flag to indidcate if we are still running
int bRunning = 1;

// The message size
int message_size = 65535;

// How long (in seconds) this should run for
int duration = 10;

// Move all the elements after arr down one
void move_down ( SOCKET *arr, SOCKET *arr_end ) {

	// Check this socket isn't already invalid
	assert ( *arr != INVALID_SOCKET );

	*arr = INVALID_SOCKET;

	// Move any other clients down
	while ( (arr + 1) < arr_end ) {
		*arr = *arr++;

		// Check we didn't just copy a INVALID_SOCKET
		assert ( *arr != INVALID_SOCKET );
	}
}

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
	//int ret = pthread_attr_setaffinity_np( attr, cpusetsize, cpuset );
	//if (ret)
	//	goto cleanup;

	ret = pthread_create(thread, attr, start_routine, arg);

cleanup:
	if ( attr = &thread_attr )
		pthread_attr_destroy ( &thread_attr );

	return ret;
}


/**
	Creates a server, and handles each incoming client
*/
void server_thread(unsigned short port) {
	SOCKET s = INVALID_SOCKET; // The listen server socket

	SOCKET client [ FD_SETSIZE - 1 ]; // We can only have 1 server socket, and (FD_SETSIZE - 1) clients
	SOCKET *c = client;
	int clients = 0; // The number of clients

	int i;

	long long bytes_recv [ FD_SETSIZE - 1 ];

	unsigned char *buffer = NULL; // Buffer to read data into, will be malloced later

	struct sockaddr_in addr; // Address to listen on

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
	addr.sin_addr.s_addr = ADDR_ANY;
	addr.sin_port = htons( port );

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

	while ( bRunning ) {
		fd_set readFD;
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

		ret = select(0, &readFD, NULL, NULL, NULL);
		if ( ret ==  SOCKET_ERROR ) {
			fprintf(stderr, "%s: %d select() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		// Did the listen socket fire?
		if ( FD_ISSET(s, &readFD) ) {
			struct sockaddr_storage addr;
			int addr_len = sizeof(addr);

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
}

/**
	Creates n client connects to address
*/
void client_thread(const struct sockaddr *addr, int addr_len, unsigned int n) {
	SOCKET client [ FD_SETSIZE ];
	SOCKET *c = client;
	SOCKET s;
	int clients = 0; // The number of clients
	
	unsigned char *buffer = NULL;

	// Blank client before we start
	for ( c = client; c < &client[ sizeof(client) / sizeof(*client) ]; c++)
		*c = INVALID_SOCKET;

	// Connect all the clients
	while ( n > 0 ) {
		//s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		s = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if ( s == INVALID_SOCKET ) {
			fprintf(stderr, "%s: %d socket() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		if ( connect( s, addr, addr_len ) == SOCKET_ERROR ) {
			fprintf(stderr, "%s: %d connect() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		client [ clients ] = s;
		clients++;

		n--;
	}

	buffer = malloc( message_size );
	memset( buffer, 'AAAA', 0 );

	// Now start the main loop
	while ( bRunning ) {
		fd_set readFD;
		fd_set writeFD;
		int ret;
		int i;

		FD_ZERO ( &readFD ); FD_ZERO ( &writeFD );

		// Loop all client sockets
		for (c = client ; c < &client [ clients ] ; c++) {
			s = *c;
			assert ( s != INVALID_SOCKET );

			// Add them to FD sets
			FD_SET( s, &readFD);
			FD_SET( s, &writeFD);
		}

		ret = select(0, &readFD, &writeFD, NULL, NULL);
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
}

int main (int argc, const char *argv[]) {

#ifdef WIN32
	WSADATA wsaData;
	struct sockaddr_in server_addr;

	if ( WSAStartup(MAKEWORD(2,2), &wsaData) ) {
		fprintf(stderr, "%s: %d WSAStartup() error\n", __FILE__, __LINE__ );
		return;
	}
#endif

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr( "127.0.0.1" );
	server_addr.sin_port = htons( 1234 );

//	server_thread ( 1234 );
	client_thread( (struct sockaddr *)&server_addr, sizeof(server_addr), 1 );

#ifdef WIN32
	WSACleanup();
#endif
}
