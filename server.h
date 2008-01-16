#include "common.h"


extern volatile unsigned int server_listen_unready;

void *server_thread(void *data);