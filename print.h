#include "common.h"

void print_headers(const struct settings* settings);
void print_results( const struct settings *settings, const struct stats *stats );
void print_stats(double sum, double sumsquare, double mean, double variance) ;

void print_hex(void *data, int size);


extern pthread_mutex_t printf_mutex;

