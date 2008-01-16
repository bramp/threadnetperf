#ifndef _GLOBAL_H
#define _GLOBAL_H

// Some global vars
extern pthread_cond_t ready_cond; // Signals control thread when a worker thread is ready
extern pthread_mutex_t ready_mutex;

extern pthread_cond_t go_cond; // Signal all threads when they are ready to start
extern pthread_mutex_t go_mutex;

extern volatile unsigned int unready_threads;

#endif
