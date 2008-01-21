#include "common.h"
#include "netlib.h"

int read_settings( SOCKET s, struct settings * settings );
int send_settings( SOCKET s, const struct settings * settings );

int read_stats( SOCKET s, struct stats * settings );
int send_stats( SOCKET s, const struct stats * settings );