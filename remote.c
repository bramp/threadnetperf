#include "remote.h"

#include "common.h"
#include "serialise.h"
#include "print.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
		fprintf(stderr, "%s:%d socket() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto bail;
	}

	// SO_REUSEADDR
	if ( setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d setsockopt(SOL_SOCKET, SO_REUSEADDR) error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto bail;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons( CONTROL_PORT );

	// Bind
	if ( bind(listen_socket, (struct sockaddr *) &addr, sizeof(addr)) == SOCKET_ERROR) {
		fprintf(stderr, "%s:%d bind() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto bail;
	}

	// Listen
	if ( listen(listen_socket, SOMAXCONN) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d listen() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
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

	closesocket_ign_signal(listen_socket);
	listen_socket = INVALID_SOCKET;

	return -1;
}

SOCKET connect_daemon(const struct settings *settings) {
	SOCKET s;
	struct sockaddr_in addr; // Address to listen on
	socklen_t addr_len = sizeof(addr);

	assert ( settings != NULL );

	s = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if ( s == INVALID_SOCKET ) {
		fprintf(stderr, "%s:%d socket() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	if ( set_socket_timeout(s, CONTROL_TIMEOUT) ) {
		fprintf(stderr, "%s:%d set_socket_timeout() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	memset(&addr, 0, sizeof(addr));
	if ( str_to_addr( settings->server_host, (struct sockaddr *)&addr, &addr_len ) ) {
		fprintf(stderr, "%s:%d str_to_addr() error\n", __FILE__, __LINE__);
		goto cleanup;
	}

	if ( addr.sin_port == 0 )
		addr.sin_port = htons( CONTROL_PORT );

	if ( settings->verbose ) {
		char addr_str[NI_MAXHOST + NI_MAXSERV + 1];

		// Print the host/port
		addr_to_ipstr((struct sockaddr *)&addr, sizeof(addr), addr_str, sizeof(addr_str));

		printf("Connecting to deamon %s\n", addr_str);
	}

	if ( connect_ign_signal(s, (struct sockaddr *)&addr, sizeof(addr) ) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d connect() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	return s;

cleanup:

	closesocket_ign_signal(s);
	return INVALID_SOCKET;
}

void close_daemon( ) {
	closesocket_ign_signal(listen_socket);
	listen_socket = INVALID_SOCKET;
}

int send_test( SOCKET s, const struct settings *settings) {
	assert ( s != INVALID_SOCKET );
	assert ( settings != NULL );

	if ( send_settings(s, settings) ) {
		fprintf(stderr, "%s:%d send_settings() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
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

	s = accept_ign_signal(listen_socket, (struct sockaddr *)&addr, &addr_len);
	if ( s == INVALID_SOCKET) {
		fprintf(stderr, "%s:%d accept() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	if ( set_socket_timeout(s, CONTROL_TIMEOUT) ) {
		fprintf(stderr, "%s:%d set_socket_timeout() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	if ( read_settings ( s, recv_settings ) ) {
		fprintf(stderr, "%s:%d read_settings() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
		goto cleanup;
	}

	if ( recv_settings->verbose ) {
		char addr_str[NI_MAXHOST + NI_MAXSERV + 1];

		// Print the host/port
		addr_to_ipstr((struct sockaddr *)&addr, sizeof(addr), addr_str, sizeof(addr_str));

		printf("Incoming control connection %s\n", addr_str);

		printf("Received tests\n");
	}

	return s;

cleanup:
	return INVALID_SOCKET;
}


int remote_setup_data(void** data, SOCKET s) {

	struct remote_data *remote_data;

	assert ( data != NULL );
	assert ( *data == NULL );

	// Malloc some space for the new data
	remote_data = malloc( sizeof( *remote_data) );
	*data = remote_data;

	if ( remote_data == NULL ) {
		return -1;
	}

	remote_data->control_socket = s;
	remote_data->stats_socket = create_stats_socket();
	
	if( remote_data->stats_socket == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d create_stats_socket() error\n", __FILE__, __LINE__ );
		return -1;
	}
	
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
		SOCKET s = ((struct remote_data*)data)->control_socket;

		if ( s != INVALID_SOCKET ) {
			// Gracefully shut down to make sure any remaining stats get sent
			shutdown ( s, SHUT_RDWR );
			closesocket_ign_signal ( s );
		}

		s = ((struct remote_data*)data)->stats_socket;
		if ( s != INVALID_SOCKET ) {
			// Gracefully shut down to make sure any remaining stats get sent
			shutdown ( s, SHUT_RDWR );
			closesocket_ign_signal ( s );
		}

		free ( data );
	}

	return 0;
}

// Receive the results from the remote daemon
int remote_collect_results(const struct settings *settings, struct stats *total_stats, int (*print_results)(const struct settings *, const struct stats *, void * data), void *data) {
	unsigned int core = 0;
	SOCKET s;

	assert ( settings != NULL );
	assert ( total_stats != NULL );
	assert ( data != NULL );

	// Currently the duration might get screwed up if not zero
	assert ( total_stats->duration == 0 );
	
	s = ((struct remote_data*)data)->control_socket;
	assert ( s != INVALID_SOCKET );

	for ( ; core < settings->servercores; core++ ) {
		struct stats stats;
		memset(&stats, 0 , sizeof(stats));
 
		if ( read_results( s, &stats )< 0) {
			fprintf(stderr, "%s:%d read_results() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
			return -1;
		}

		if ( print_results(settings, &stats, data) ) {
			fprintf(stderr, "%s:%d print_results() error\n", __FILE__, __LINE__ );
			return -1;
		}

		stats_add( total_stats, &stats );
	}

	total_stats->duration /= settings->servercores;

	return 0;
}

int remote_send_results (const struct settings *settings, const struct stats *stats, void * data) {
	SOCKET s = ((struct remote_data*)data)->control_socket;

	assert ( data != NULL );
	assert ( s != INVALID_SOCKET );

	return send_results(s, stats);
}

int signal_remote( SOCKET s, unsigned char code ) {
	assert ( s != INVALID_SOCKET );
	return send_ign_signal(s, &code, 1, 0) != 1;
}

int wait_remote( SOCKET s, unsigned char code ) {
	unsigned char code2;
	assert ( s != INVALID_SOCKET );

	if ( recv_ign_signal(s, &code2, 1, 0) == SOCKET_ERROR ) {
		fprintf(stderr, "%s:%d recv() error (%d) %s\n", __FILE__, __LINE__, ERRNO, strerror(ERRNO) );
	} else if ( code2 != code ) {
		fprintf(stderr, "%s:%d wait_remote() invalid code (expected %d got %d)\n", __FILE__, __LINE__, code, code2 );
	} else {
		return 0;
	}
	
	return -1;
}

int signal_ready( const struct settings *settings, void *data ) {
	SOCKET s;
	assert ( data != NULL );
	s = ((struct remote_data*)data)->control_socket;
	assert ( s != INVALID_SOCKET );

	return signal_remote( s, SIGNAL_READY_TO_GO );
}

int signal_go( const struct settings *settings, void *data ) {
	SOCKET s;
	assert ( data != NULL );
	s = ((struct remote_data*)data)->control_socket;
	assert ( s != INVALID_SOCKET );

	return signal_remote( s, SIGNAL_GO );
}

int wait_ready( const struct settings *settings, void *data ) {
	SOCKET s;
	assert ( data != NULL );
	s = ((struct remote_data*)data)->control_socket;
	assert ( s != INVALID_SOCKET );

	return wait_remote( s, SIGNAL_READY_TO_GO );
}

int wait_go ( const struct settings *settings, void *data ) {
	SOCKET s;
	assert ( data != NULL );
	s = ((struct remote_data*)data)->control_socket;
	assert ( s != INVALID_SOCKET );

	return wait_remote( s, SIGNAL_GO );
}
