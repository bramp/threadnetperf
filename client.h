#include "common.h"

extern volatile unsigned int server_listen_unready;

// Struct to pass to a client thread
struct client_request {

	volatile int bRunning; // Flag to indicate if the client should be running

	unsigned int cores; // Which cores this client is running on

	const struct settings *settings;

	struct client_request_details *details;
};

// One of these for each destination this client thread connects to
struct client_request_details {

	// The address to connect to
	struct sockaddr_storage addr;
	socklen_t addr_len;

	unsigned int n; // The number of connection to create

	struct client_request_details *next;
};

void *client_thread(void *data);

int prepare_clients(const struct settings * settings, void * data);
int create_clients(const struct settings * settings, void * data);

void stop_all_clients();
void cleanup_clients();
