#include "common.h"
#include "netlib.h"

int read_settings( SOCKET s, struct settings * settings );
int send_settings( SOCKET s, const struct settings * settings );
