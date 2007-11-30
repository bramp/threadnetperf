
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <malloc.h>

#ifdef WIN32
	#define WIN32_LEAN_AND_MEAN
	#include "winsock2.h"
	#define ERRNO (WSAGetLastError())
	#define ECONNRESET WSAECONNRESET

#else
	#define ERRNO errno
	#define closesocket(s) close(s)

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
	s = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP);
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
		fd_set errorFD;
		int ret;

		FD_ZERO( &readFD ); FD_ZERO( &errorFD );

		// Add the listen socket (only if we have room for more clients)
		if ( clients < sizeof(client) / sizeof(*client) )
			FD_SET(s, &readFD); FD_SET(s, &errorFD);

		// Add all the client sockets
		for (c = client ; c < &client [ clients] ; c++) {
			assert ( *c != INVALID_SOCKET );

			FD_SET( *c, &readFD);
			FD_SET( *c, &errorFD);
		}

		ret = select(0, &readFD, NULL, &errorFD, NULL);
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
		if ( FD_ISSET(s, &errorFD) ) {
			fprintf(stderr, "%s: %d error with server socket %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		// Figure out which sockets have fired
		i = 0;
		while ( ret > 0 ) {
			SOCKET s = client [ i ];
			int socketRemoved = FALSE;

			assert ( i < sizeof( client ) / sizeof( *client) );
			assert ( s  != INVALID_SOCKET );

			// Check for reads
			if ( FD_ISSET( s, &readFD) ) {
				int len = recv( s, buffer, message_size, 0);

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
					socketRemoved = TRUE;

				} else {
					// We could dirty the buffer

					// Count how many bytes have been received
					bytes_recv [ i ] += len;
				}

				ret--;
			}
			// Check for errors
			if ( FD_ISSET( s, &errorFD) ) {
				
				// Apparently this will never be called

				/*
				// Invalid this client (if it hasn't already been removed)
				if ( !socketRemoved ) {
					closesocket( s );
					move_down ( &client[ i ], &client[ clients ] );
					clients--;
					socketRemoved = TRUE;
				}

				ret--;
				*/
				assert ( 0 );
				ret--;
			}

			// Move the socket on (if needed)
			if (!socketRemoved)
				i++;
		}
	}

cleanup:
	// Cleanup
	if ( buffer )
		free( buffer );

	// Shutdown server socket
	if ( s != INVALID_SOCKET ) {
		shutdown ( s, SD_BOTH );
		closesocket( s );
	}

	// Shutdown client sockets
	for (c = client ; c < &client [ clients ] ; c++) {
		s = *c;
		if ( s != INVALID_SOCKET ) {
			shutdown ( s, SD_BOTH );
			closesocket( s );
			clients--;
		}
	}
	
	assert ( clients == 0 );
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
		fd_set errorFD;
		int ret;
		int i;

		FD_ZERO ( &readFD ); FD_ZERO ( &writeFD ); FD_ZERO ( &errorFD );

		// Loop all client sockets
		for (c = client ; c < &client [ clients ] ; c++) {
			s = *c;
			assert ( s != INVALID_SOCKET );

			// Add them to FD sets
			FD_SET( s, &readFD);
			FD_SET( s, &writeFD);
			FD_SET( s, &errorFD);
		}

		ret = select(0, &readFD, &writeFD, &errorFD, NULL);
		if ( ret ==  SOCKET_ERROR ) {
			fprintf(stderr, "%s: %d select() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		// Figure out which sockets have fired
		i = 0;
		while ( ret > 0 ) {
			SOCKET s = client [ i ];
			int socketRemoved = FALSE;

			assert ( i < sizeof( client ) / sizeof( *client) );
			assert ( s  != INVALID_SOCKET );

			// Check for reads
			if ( FD_ISSET( s, &readFD) ) {
				int len = recv( s, buffer, message_size, 0);

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
					socketRemoved = TRUE;

				}

				ret--;
			}
			
			// Check if we are ready to write
			if ( FD_ISSET( s, &writeFD) ) {
				if ( send( s, buffer, message_size, 0 ) == SOCKET_ERROR ) {
					fprintf(stderr, "%s: %d send() error %d\n", __FILE__, __LINE__, ERRNO );
					goto cleanup;
				}

				ret--;
			}

			// Check for errors
			if ( FD_ISSET( s, &errorFD) ) {
				
				/*
				// Invalid this client (if it hasn't already been removed)
				if ( !socketRemoved ) {
					closesocket( s );
					move_down ( &client[ i ], &client[ clients ] );
					clients--;
					socketRemoved = TRUE;
				}
				*/
				assert ( 0 );

				ret--;
			}

			// Move the socket on (if needed)
			if (!socketRemoved)
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
			shutdown ( s, SD_BOTH );
			closesocket( s );
			clients--;
		}
	}
	
	assert ( clients == 0 );
}

int main (int argc, const char *argv[]) {
	WSADATA wsaData;
	struct sockaddr_in server_addr;

	if ( WSAStartup(MAKEWORD(2,2), &wsaData) ) {
		fprintf(stderr, "%s: %d WSAStartup() error\n", __FILE__, __LINE__ );
		return;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr( "127.0.0.1" );
	server_addr.sin_port = htons( 1234 );

//	server_thread ( 1234 );
	client_thread( (struct sockaddr *)&server_addr, sizeof(server_addr), 1 );

	WSACleanup();
}