#include "remote.h"

#include "common.h"
#include "serialise.h"
#include "print.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#ifndef WIN32
#include <unistd.h>
#endif

SOCKET listen_socket = INVALID_SOCKET;

// Creates a socket
int start_daemon(const struct settings * settings) {

	struct sockaddr_in addr; // Address to listen on
	int one = 1;

	assert ( settings != NULL );
	assert ( listen_socket == INVALID_SOCKET );

	listen_socket = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP);

	if ( listen_socket == INVALID_SOCKET ) {
		fprintf(stderr, "%s:%d socket() error %d\n", __FILE__, __LINE__, ERRNO );
		goto bail;
	}

	// SO_REUSEADDR
	if ( setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d setsockopt(SOL_SOCKET, SO_REUSEADDR) error %d\n", __FILE__, __LINE__, ERRNO );
		goto bail;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( CONTROL_PORT );

	// Bind
	if ( bind(listen_socket, (struct sockaddr *) &addr, sizeof(addr)) == SOCKET_ERROR) {
		fprintf(stderr, "%s:%d bind() error %d\n", __FILE__, __LINE__, ERRNO );
		goto bail;
	}

	// Listen
	if ( listen(listen_socket, SOMAXCONN) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d listen() error %d\n", __FILE__, __LINE__, ERRNO );
		goto bail;
	}

	if ( settings->verbose ) {
		char addr_str[NI_MAXHOST + NI_MAXSERV + 1];

		// Print the host/port
		addr_to_ipstr((struct sockaddr *)&addr, sizeof(addr), addr_str, sizeof(addr_str));

		printf("Deamon is listening on %s\n", addr_str);
	}

	return 0;

bail:

	closesocket(listen_socket);
	listen_socket = INVALID_SOCKET;

	return -1;
}

SOCKET connect_daemon(const struct settings *settings) {
	SOCKET s;
	struct sockaddr_in addr; // Address to listen on

	assert ( settings != NULL );

	s = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if ( s == INVALID_SOCKET ) {
		fprintf(stderr, "%s:%d socket() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( set_socket_timeout(s, CONTROL_TIMEOUT) ) {
		fprintf(stderr, "%s:%d set_socket_timeout() error %d\n", __FILE__, __LINE__, ERRNO );
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

	return s;

cleanup:

	closesocket(s);
	return INVALID_SOCKET;
}

void close_daemon( ) {
	closesocket(listen_socket);
	listen_socket = INVALID_SOCKET;
}

int send_test( SOCKET s, const struct settings *settings) {
	assert ( s != INVALID_SOCKET );
	assert ( settings != NULL );

	if ( send_settings(s, settings) ) {
		fprintf(stderr, "%s:%d send_settings() error %d\n", __FILE__, __LINE__, ERRNO );
		return -1;
	}
	
	return 0;
}

SOCKET accept_test( SOCKET listen_socket, struct settings *recv_settings) {
	SOCKET s = INVALID_SOCKET;

	struct sockaddr_storage addr; // Incoming addr
	socklen_t addr_len = sizeof(addr);

	assert ( listen_socket != INVALID_SOCKET );
	assert ( recv_settings != NULL );

	s = accept(listen_socket, (struct sockaddr *)&addr, &addr_len);
	if ( s == INVALID_SOCKET) {
		fprintf(stderr, "%s:%d accept() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( set_socket_timeout(s, CONTROL_TIMEOUT) ) {
		fprintf(stderr, "%s:%d set_socket_timeout() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( recv_settings->verbose ) {
		char addr_str[NI_MAXHOST + NI_MAXSERV + 1];

		// Print the host/port
		addr_to_ipstr((struct sockaddr *)&addr, sizeof(addr), addr_str, sizeof(addr_str));

		printf("Incoming control connection %s\n", addr_str);
	}

	if ( read_settings ( s, recv_settings ) ) {
		fprintf(stderr, "%s:%d read_settings() error %d\n", __FILE__, __LINE__, ERRNO );
		goto cleanup;
	}

	if ( recv_settings->verbose )
		printf("Received tests\n");

	return s;

cleanup:
	return INVALID_SOCKET;
}

struct remote_data {
	SOCKET s;
};

int remote_setup_data(void** data, SOCKET s) {

	struct remote_data *remote_data;

	assert ( data != NULL );
	assert ( *data == NULL );
	assert ( s != INVALID_SOCKET );

	// Malloc some space for the new data
	remote_data = malloc( sizeof( *remote_data) );
	*data = remote_data;

	if ( remote_data == NULL ) {
		return -1;
	}

	remote_data->s = s;
	return 0;
}

int remote_accept(struct settings *settings, void **data) {
	SOCKET s = INVALID_SOCKET;

	// Wait for a test to come in
	s = accept_test( listen_socket, settings );
	if ( s == INVALID_SOCKET )
		return -1;

	if ( remote_setup_data(data, s) ) {
		closesocket(s);
		return -1;
	}

	return 0;
}

// Connect to a remote daemon and send the test
int remote_connect(struct settings *settings, void** data) {
	SOCKET s = INVALID_SOCKET;

	assert ( settings != NULL );

	s = connect_daemon(settings);
	if ( s == INVALID_SOCKET ) {
		return -1;
	}

	if ( send_test( s, settings) ) {
		fprintf(stderr, "%s:%d send_test() error\n", __FILE__, __LINE__ );
		closesocket(s);
		return -1;
	}

	if ( remote_setup_data(data, s) ) {
		closesocket(s);
		return -1;
	}

	return 0;
}

int remote_cleanup(const struct settings *settings, void* data) {

	assert ( settings != NULL );
	
	if (data) {
		SOCKET s = ((struct remote_data*)data)->s;
		
		if ( s != INVALID_SOCKET ) {
			// Gracefully shut down to make sure any remaining stats get sent
			shutdown ( s, SD_BOTH );
			closesocket ( s );
		}
		free ( data );
	}

	return 0;
}

// Receive the results from the remote daemon
int remote_collect_results(const struct settings *settings, struct stats *total_stats, int (*print_results)(const struct settings *, const struct stats *, void * data), void *data) {
	unsigned int core = 0;
	SOCKET s = ((struct remote_data*)data)->s;

	assert ( data != NULL );
	assert ( s != INVALID_SOCKET );

	for ( ; core < settings->cores + 1; core++ ) {
		struct stats stats;

		if ( read_results( s, &stats ) ) {
			fprintf(stderr, "%s:%d read_results() error %d\n", __FILE__, __LINE__, ERRNO );
			return -1;
		}

		print_results(settings, &stats, data);

		// Quit looking for more results if this is the total
		if ( stats.core == ~0 ) {
			*total_stats = stats;
			break;
		}
	}

	return 0;
}

int remote_send_results (const struct settings *settings, const struct stats *stats, void * data) {
	SOCKET s = ((struct remote_data*)data)->s;

	assert ( data != NULL );
	assert ( s != INVALID_SOCKET );

	return send_results(s, stats);
}

#define SIGNAL_READY 1
#define SIGNAL_GO 2
#define SIGNAL_STOP 3

int signal_remote( SOCKET s, unsigned char code ) {
	assert ( s != INVALID_SOCKET );
	return send(s, &code, 1, 0) != 1;
}

int wait_remote( SOCKET s, unsigned char code ) {
	unsigned char code2;
	assert ( s != INVALID_SOCKET );

	if ( recv(s, &code2, 1, 0) != 1 || code2 != code )
		return -1;

	return 0;
}

int signal_ready( const struct settings *settings, void *data ) {
	SOCKET s;
	assert ( data != NULL );
	s = ((struct remote_data*)data)->s;
	assert ( s != INVALID_SOCKET );

	return signal_remote( s, SIGNAL_READY );
}

int signal_go( const struct settings *settings, void *data ) {
	SOCKET s;
	assert ( data != NULL );
	s = ((struct remote_data*)data)->s;
	assert ( s != INVALID_SOCKET );

	return signal_remote( s, SIGNAL_GO );
}

int wait_ready( const struct settings *settings, void *data ) {
	SOCKET s;
	assert ( data != NULL );
	s = ((struct remote_data*)data)->s;
	assert ( s != INVALID_SOCKET );

	return wait_remote( s, SIGNAL_READY );
}

int wait_go ( const struct settings *settings, void *data ) {
	SOCKET s;
	assert ( data != NULL );
	s = ((struct remote_data*)data)->s;
	assert ( s != INVALID_SOCKET );

	return wait_remote( s, SIGNAL_GO );
}

