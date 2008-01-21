#include "common.h"

// Control port for the deamon, make this a option
#define CONTROL_PORT 0xABCD

// Starts a control daemon
SOCKET start_daemon(const struct settings * settings);

// Accept a incoming connection
SOCKET accept_test( SOCKET listen_socket, struct settings *recv_settings, int verbose);

// Sends a test to a daemon
int send_test( SOCKET socket, const struct settings *settings);

// Close the control daemon
void close_daemon( SOCKET listen_socket );

// Connect to a control daemon and send these settings
SOCKET connect_daemon(const struct settings *settings);

int signal_ready( const struct settings *settings, void *data );
int signal_go   ( const struct settings *settings, void *data );

int wait_ready( const struct settings *settings, void *data );
int wait_go   ( const struct settings *settings, void *data );

int remote_connect(const struct settings *settings, void** data);
int remote_cleanup(const struct settings *settings, void* data);

int remote_collect_results(const struct settings *settings, struct stats *total_stats, int (*print_results)(const struct settings *, struct stats *, void * data), void *data);
int remote_send_results (const struct settings *settings, const struct stats *stats, void * data);
