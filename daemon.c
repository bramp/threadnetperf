#include "daemon.h"

#include "common.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// Creates a socket and lists for incoming test requests
void start_daemon(const struct settings * settings) {
	//unready_threads = 0; // Number of threads not ready

	SOCKET listen_socket = INVALID_SOCKET;
	SOCKET s = INVALID_SOCKET; // Incoming socket

	struct sockaddr_in addr; // Address to listen on
	int one = 1;

	assert ( settings != NULL );

	listen_socket = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP);

	if ( listen_socket == INVALID_SOCKET ) {
		fprintf(stderr, "%s:%d socket() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	// SO_REUSEADDR
	if ( setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d setsockopt(SOL_SOCKET, SO_REUSEADDR) error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( CONTROL_PORT );

	// Bind
	if ( bind(listen_socket, (struct sockaddr *) &addr, sizeof(addr)) == SOCKET_ERROR) {
		fprintf(stderr, "%s:%d bind() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	// Listen
	if ( listen(listen_socket, SOMAXCONN) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d listen() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( settings->verbose ) {
		char addr_str[NI_MAXHOST + NI_MAXSERV + 1];

		// Print the host/port
		addr_to_ipstr((struct sockaddr *)&addr, sizeof(addr), addr_str, sizeof(addr_str));

		printf("Deamon is listening on %s\n", addr_str);
	}

	// Now loop accepting incoming tests
	while ( 1 ) {
		struct sockaddr_storage addr; // Incoming addr
		socklen_t addr_len = sizeof(addr);

		struct settings recv_settings; // Incoming settings

		int ret;

		s = accept(listen_socket, (struct sockaddr *)&addr, &addr_len);
		if ( s == INVALID_SOCKET) {
			fprintf(stderr, "%s:%d accept() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		if ( settings->verbose ) {
			char addr_str[NI_MAXHOST + NI_MAXSERV + 1];

			// Print the host/port
			addr_to_ipstr((struct sockaddr *)&addr, sizeof(addr), addr_str, sizeof(addr_str));

			printf("Incoming control connection %s\n", addr_str);
		}

		ret = recv(s, (char *)&recv_settings, sizeof(recv_settings), 0);
		if ( ret != sizeof(recv_settings) || recv_settings.version != SETTINGS_VERSION ) {

			if ( ret > 0 ) {
				fprintf(stderr, "Invalid setting struct received\n" );
				goto cleanup;
			} 

			fprintf(stderr, "%s:%d recv() error %d\n", __FILE__, __LINE__, ERRNO );
			goto cleanup;
		}

		if ( settings->verbose ) {
			printf("Received tests\n");
		}

	}

cleanup:

	closesocket(listen_socket);
	closesocket(s);
}

void connect_daemon(const struct settings *settings) {
	SOCKET s;
	struct sockaddr_in addr; // Address to listen on
	int ret;

	assert ( settings != NULL );

	s = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if ( s == INVALID_SOCKET ) {
		fprintf(stderr, "%s:%d socket() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr( settings->server_host );
	addr.sin_port = htons( CONTROL_PORT );

	if ( settings->verbose ) {
		char addr_str[NI_MAXHOST + NI_MAXSERV + 1];

		// Print the host/port
		addr_to_ipstr((struct sockaddr *)&addr, sizeof(addr), addr_str, sizeof(addr_str));

		printf("Connecting to deamon %s\n", addr_str);
	}

	if ( connect(s, (struct sockaddr *)&addr, sizeof(addr) ) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d connect() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( settings->verbose )
		printf("Connect to deamon, sending tests\n");

	ret = send(s, (const char *)settings, sizeof(*settings), 0 );
	if ( ret != sizeof(*settings) ) {
		fprintf(stderr, "%s:%d send() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( settings->verbose )
		printf("Sent tests\n");

cleanup:

	closesocket(s);
}
