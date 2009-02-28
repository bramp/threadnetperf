#ifndef _GLOBAL_H
#define _GLOBAL_H

#include "threads.h"

// Some global vars
extern pthread_cond_t go_cond; // Signal all threads when they are ready to start
extern pthread_mutex_t go_mutex;

extern const unsigned int max_cores; // The number of CPU cores this machine has

extern volatile int bRunning; // Flag to indidcate if we are still running
extern volatile int bGo; // Flag to indidcate if we can start the test

#endif
