#ifndef _GLOBAL_H
#define _GLOBAL_H

// Some global vars
extern pthread_cond_t ready_cond;
extern pthread_cond_t go_cond;
extern pthread_mutex_t go_mutex;

extern volatile unsigned int unready_threads;

#endif
