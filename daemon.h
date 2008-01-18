#include "common.h"

// Control port for the deamon, make this a option
#define CONTROL_PORT 0xABCD

// Starts a control daemon
SOCKET start_daemon(const struct settings * settings);

// Accept a incoming connection
SOCKET accept_test( SOCKET listen_socket, struct settings *recv_settings, int verbose);

// Close the control daemon
void close_daemon( SOCKET listen_socket );

// Connect to a control daemon and send these settings
void connect_daemon(const struct settings *settings);
