#ifndef _GLOBAL_H
#define _GLOBAL_H

// Some global vars
extern pthread_cond_t ready_cond; // Signals control thread when a worker thread is ready
extern pthread_mutex_t ready_mutex;

extern pthread_cond_t go_cond; // Signal all threads when they are ready to start
extern pthread_mutex_t go_mutex;

extern const unsigned int max_cores; // The number of CPU cores this machine has
extern unsigned int unready_threads; // Count of how many threads are not ready

extern pthread_t *thread; // Array to handle thread handles
extern unsigned int threads; // Total number of threads

extern volatile int bRunning; // Flag to indidcate if we are still running
extern volatile int bGo; // Flag to indidcate if we can start the test

#endif
