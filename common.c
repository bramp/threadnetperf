#include "common.h"

#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <malloc.h>

#ifndef WIN32
#include <time.h>
#else
#include <pthread.h> // for timespec
#endif

const unsigned int max_cores = 8; // TODO get the real number!

// Works out how many cores the client will use
unsigned int count_client_cores( const struct test *test, const unsigned int tests ){

	unsigned int bins = 0;
	unsigned int *bin;
	unsigned int i = 0;

	assert (test != NULL);

	// Malloc space for the max number of core combinations
	bin = calloc(tests, sizeof(*bin)); 
	if ( bin == NULL )
		return -1;

	for ( i = 0; i < tests; i++ ) {
		unsigned int b = 0;

		// Find the bin this test is in
		for ( ; b < bins; b++) {
			if ( bin[ b ] == test[i].clientcores )
				break;
		}

		// Break if we found this bin early
		if ( b < bins )
			break;

		bin[bins] = test[i].clientcores;
		bins++;
	}

	free(bin);

	return bins;
}

// Works out how many cores the server will use
unsigned int count_server_cores( const struct test *test, const unsigned int tests ) {

	unsigned int bins = 0;
	unsigned int *bin;
	unsigned int i = 0;

	assert (test != NULL);

	bin = calloc(tests, sizeof(*bin)); 
	if ( bin == NULL )
		return -1;

	for ( i = 0; i < tests; i++ ) {
		unsigned int b = 0;

		// Find the bin this test is in
		for ( ; b < bins; b++) {
			if ( bin[ b ] == test[i].servercores )
				break;
		}

		// Break if we found this bin early
		if ( b < bins )
			break;

		bin[bins] = test[i].servercores;
		bins++;
	}

	free(bin);

	return bins;
}

void stats_add(struct stats *dest, const struct stats *src) {
	assert( dest != NULL );
	assert( src != NULL );

	dest->bytes_received += src->bytes_received;
	dest->duration       += src->duration;
	dest->pkts_received  += src->pkts_received;
	dest->pkts_time      += src->pkts_time;
	dest->timestamps     += src->timestamps;

}

// Move all the elements after arr down one
void move_down ( SOCKET *arr, SOCKET *arr_end ) {

	// Check this socket isn't already invalid
	assert ( arr != NULL );
	assert ( arr_end != NULL );
	assert ( arr < arr_end );
	assert ( *arr != INVALID_SOCKET );

	*arr = INVALID_SOCKET;

	// Move any other clients down
	while ( (arr + 1) < arr_end ) {
		*arr = *(arr + 1);
		arr++;

		// Check we didn't just copy a INVALID_SOCKET
		assert ( *arr != INVALID_SOCKET );
	}
}

/**
	Returns the number of microseconds since a epoch
		On windows the epoch is January 1, 1601 (UTC)
		On linux the eopoch is January 1, 1970 (UTC)
**/

unsigned long long get_microseconds() {
	unsigned long long microseconds = 0;

#ifdef WIN32
	FILETIME ft;

	GetSystemTimeAsFileTime(&ft);

	microseconds |= ft.dwHighDateTime;
	microseconds <<= 32;
	microseconds |= ft.dwLowDateTime;

	microseconds /= 10;	//convert into microseconds
#else
	struct timeval tv;

	gettimeofday(&tv, NULL);
	microseconds = tv.tv_sec * 1000000 + tv.tv_usec;

#endif

	return microseconds;
}

unsigned long long get_nanoseconds() {
	unsigned long long nanoseconds = 0;

#ifdef WIN32
	FILETIME ft;

	GetSystemTimeAsFileTime(&ft);

	nanoseconds  = ft.dwHighDateTime;
	nanoseconds <<= 32;
	nanoseconds |= ft.dwLowDateTime;

	nanoseconds *= 10;	//convert into nanoseconds
#else
	struct timespec ts;

	//getnstimeofday(&ts, NULL);
	clock_gettime(CLOCK_REALTIME, &ts);
	nanoseconds = ts.tv_sec * 1000000000 + ts.tv_nsec;
#endif

	return nanoseconds;	
}

#ifdef WIN32
#define TIMESPEC_TO_FILETIME_OFFSET (((LONGLONG)27111902 << 32) + (LONGLONG)3577643008)

static void timespec_to_filetime(const struct timespec *ts, FILETIME *ft) {
	*(LONGLONG *)ft = ts->tv_sec * 10000000 + (ts->tv_nsec + 50) / 100 + TIMESPEC_TO_FILETIME_OFFSET;
}

static void filetime_to_timespec(const FILETIME *ft, struct timespec *ts) {
	ts->tv_sec = (int)((*(LONGLONG *)ft - TIMESPEC_TO_FILETIME_OFFSET) / 10000000);
	ts->tv_nsec = (int)((*(LONGLONG *)ft - TIMESPEC_TO_FILETIME_OFFSET - ((LONGLONG)ts->tv_sec * (LONGLONG)10000000)) * 100);
}
#endif

/**
	Returns now as a timespec value
**/
void get_timespec_now(struct timespec *ts) {
#ifdef WIN32
	FILETIME ft;

	GetSystemTimeAsFileTime(&ft);
	filetime_to_timespec( &ft, ts );
#else
	clock_gettime(CLOCK_REALTIME, ts);
#endif
}


void free_2D(void **data, size_t x) {
	// TODO make it so we don't need a x or y
	if ( data ) {
		size_t i = 0;

		for (; i < x; ++i)
			free ( data[i] );

		free( data );
	}
}

void **malloc_2D(size_t element_size, size_t x, size_t y) {

	void **data;
	size_t x1;

	if ( x == 0 || y == 0 )
		return NULL;

	// Malloc space for a 2D array
	data = calloc ( x, sizeof(*data) );
	if ( data == NULL ) {
		fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
		return NULL;
	}

	for (x1 = 0; x1 < x; x1++) {
		data[x1] = calloc ( y, element_size );
		if ( data[x1] == NULL ) {
			fprintf(stderr, "%s:%d calloc() error\n", __FILE__, __LINE__ );
			goto bail;
		}
	}

	return data;

bail:
	free_2D ( data, x );
	return NULL;
}

/*
 * Code taken from
 *
 * http://www.owlnet.rice.edu/~elec428/projects/tinv.c
 *
 * tinv(p,dof) returns the inverse t-distribution value for probability
 * p and dof degrees of freedom.  It does this by looking up the appropriate
 * value in the array tinv_array.  Only dof between 1 and 20 are exact.
 * For dof > 20, tinv() returns approximations.  Also, only probabilities
 * of 0.75, 0.9, 0.95, 0.975, 0.99, 0.995, and 0.9995 are provided.
 *
 * If p is not one of the 7 provided values, or if dof is not greater than or
 * equal to 1, tinv() returns 0.
 */

double tinv(double p, unsigned int dof)  {
	int dofindex, pindex;

	double tinv_array[31][7] = {
	{1.0000, 3.0777, 6.3138, 12.7062, 31.8207, 63.6574, 636.6192}, /* 1 */
	{0.8165, 1.8856, 2.9200, 4.3027, 6.9646, 9.9248, 31.5991}, /* 2 */
	{0.7649, 1.6377, 2.3534, 3.1824, 4.5407, 5.8409, 12.9240}, /* 3 */
	{0.7407, 1.5332, 2.1318, 2.7764, 3.7649, 4.6041, 8.6103},  /* 4 */
	{0.7267, 1.4759, 2.0150, 2.5706, 3.3649, 4.0322, 6.8688},  /* 5 */
	{0.7176, 1.4398, 1.9432, 2.4469, 3.1427, 3.7074, 5.9588},  /* 6 */
	{0.7111, 1.4149, 1.8946, 2.3646, 2.9980, 3.4995, 5.4079},  /* 7 */
	{0.7064, 1.3968, 1.8595, 3.3060, 2.8965, 3.3554, 5.0413},  /* 8 */
	{0.7027, 1.3830, 1.8331, 2.2622, 2.8214, 3.2498, 4.7809},  /* 9 */
	{0.6998, 1.3722, 1.8125, 2.2281, 2.7638, 1.1693, 4.5869},  /* 10 */
	{0.6974, 1.3634, 1.7959, 2.2010, 2.7181, 3.1058, 4.4370},  /* 11 */
	{0.6955, 1.3562, 1.7823, 2.1788, 2.6810, 3.0545, 4.3178},  /* 12 */
	{0.6938, 1.3502, 1.7709, 2.1604, 2.6403, 3.0123, 4.2208},  /* 13 */
	{0.6924, 1.3450, 1.7613, 2.1448, 2.6245, 2.9768, 4.1405},  /* 14 */
	{0.6912, 1.3406, 1.7531, 2.1315, 2.6025, 2.9467, 4.0728},  /* 15 */
	{0.6901, 1.3368, 1.7459, 2.1199, 2.5835, 2.9208, 4.0150},  /* 16 */
	{0.6892, 1.3334, 1.7396, 2.1098, 2.5669, 2.8982, 3.9651},  /* 17 */
	{0.6884, 1.3304, 1.7341, 2.1009, 2.5524, 2.8784, 3.9216},  /* 18 */
	{0.6876, 1.3277, 1.7291, 2.0930, 2.5395, 2.8609, 3.8834},  /* 19 */
	{0.6870, 1.3253, 1.7247, 2.0860, 2.5280, 2.8453, 3.8495},  /* 20 */
	{0.6844, 1.3163, 1.7081, 2.0595, 2.4851, 2.7874, 3.7251},  /* 25 */
	{0.6828, 1.3104, 1.6973, 2.0423, 2.4573, 2.7500, 3.6460},  /* 30 */
	{0.6816, 1.3062, 1.6896, 2.0301, 2.4377, 2.7238, 3.5911},  /* 35 */
	{0.6807, 1.3031, 1.6839, 2.0211, 2.4233, 2.7045, 3.5510},  /* 40 */
	{0.6800, 1.3006, 1.6794, 2.0141, 2.4121, 2.6896, 3.5203},  /* 45 */
	{0.6794, 1.2987, 1.6759, 2.0086, 2.4033, 2.6778, 3.4960},  /* 50 */
	{0.6786, 1.2958, 1.6706, 2.0003, 2.3901, 2.6603, 3.4602},  /* 60 */
	{0.6780, 1.2938, 1.6669, 1.9944, 2.3808, 2.6479, 3.4350},  /* 70 */
	{0.6776, 1.2922, 1.6641, 1.9901, 2.3739, 2.6387, 3.4163},  /* 80 */
	{0.6772, 1.2910, 1.6620, 1.9867, 2.3685, 2.6316, 3.4019},  /* 90 */
	{0.6770, 1.2901, 1.6602, 1.9840, 2.3642, 2.6259, 3.3905}}; /* 100 */

	if (dof >= 1 && dof <= 20) dofindex = dof-1;
	else if (dof < 25) dofindex = 19;
	else if (dof < 30) dofindex = 20;
	else if (dof < 35) dofindex = 21;
	else if (dof < 40) dofindex = 22;
	else if (dof < 45) dofindex = 23;
	else if (dof < 50) dofindex = 24;
	else if (dof < 60) dofindex = 25;
	else if (dof < 70) dofindex = 26;
	else if (dof < 80) dofindex = 27;
	else if (dof < 90) dofindex = 28;
	else if (dof < 100) dofindex = 29;
	else if (dof >= 100) dofindex = 30;
	else {
		return 0;
	}

	if (p == 75.0)       pindex = 0;
	else if (p == 90.0)  pindex = 1;
	else if (p == 95.0)  pindex = 2;
	else if (p == 97.5)  pindex = 3;
	else if (p == 99.0)  pindex = 4;
	else if (p == 99.5)  pindex = 5;
	else if (p == 99.95) pindex = 6;
	else {
		return 0;
	}

	return(tinv_array[dofindex][pindex]);
}

double calc_confidence(double confidence_lvl, double mean, double variance, unsigned int n, int verbose) {
	double bigZ;
	double sd_div_samples;
	double CI;

	if(variance == 0.0)
		return 0;

	bigZ = tinv(confidence_lvl, n);

	if(bigZ == 0.0) {
		fprintf(stderr, "%s:%d tinv(%f, %d) error\n", __FILE__, __LINE__, confidence_lvl, n );
		return 0;
	}

	sd_div_samples = sqrt(variance / n);

	CI = bigZ * sd_div_samples;

	if(verbose) {
		double min, max;
		double confidence;

		min = mean - CI;
		max = mean + CI;
		confidence = (1 - ( CI / mean))*100;
		printf("min: %.f, max: %.f, cl: %f\n", min, max, confidence);
	}

	return CI;
}
