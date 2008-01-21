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

int signal_ready( SOCKET s );
int signal_go( SOCKET s );
int signal_stop( SOCKET s );

int wait_ready( SOCKET s );
int wait_go ( SOCKET s );
int wait_stop ( SOCKET s );

