#ifndef _GLOBAL_H
#define _GLOBAL_H

// Some global vars
extern pthread_cond_t ready_cond;
extern pthread_cond_t go_cond;
extern pthread_mutex_t go_mutex;

extern volatile int bRunning;
extern int unready_threads;

// Settings
extern int message_size;
extern int duration;
extern int disable_nagles;

#endif