#include "common.h"

void print_headers(const struct settings* settings);
void print_results( const struct settings *settings, const struct stats *stats );
void print_stats(float sum, float sumsquare, float mean, float variance) ;

void print_hex(void *data, int size);


extern pthread_mutex_t printf_mutex;

