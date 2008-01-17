#include "common.h"

extern volatile unsigned int server_listen_unready;

struct server_request {
 
	volatile int bRunning; // Flag to indicate if the server should be running

	unsigned short port; // The port the server is listening on
	unsigned int n; // The number of connections to accept

	unsigned int core; // Which core this server is running on

	const struct settings *settings;

	// Stats
	struct stats stats;
};

void *server_thread(void *data);

int prepare_servers(const struct settings * settings);
int create_servers();
void stop_all_servers();
void cleanup_servers();
