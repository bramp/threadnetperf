#include "common.h"

// Control port for the deamon, make this a option
#define CONTROL_PORT 0xABCD

void start_daemon(const struct settings * settings);
void connect_daemon(const struct settings *settings);
