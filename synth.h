#ifndef SYNTH_H
#define SYNTH_H
#include <pthread.h>

typedef struct synth_thread_data {
	char alive;
	pthread_mutex_t alive_mtx;

} synth_thread_data;
void *synth_main_loop(void *synth_data);
void set_mod_value(int mod_id, float val);

#endif
