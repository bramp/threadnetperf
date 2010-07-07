#include "common.h"
#include "netlib.h"

int read_settings( SOCKET s, struct settings * settings );
int send_settings( SOCKET s, const struct settings * settings );

int read_results( SOCKET s, struct stats * stats );
int send_results( SOCKET s, const struct stats * stats );
