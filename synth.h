#ifndef SYNTH_H
#define SYNTH_H
#include <pthread.h>

typedef struct synth_thread_data {
	char alive;
	pthread_mutex_t alive_mtx;

} synth_thread_data;
void *synth_main_loop(void *synth_data);
void set_mod_cst_value(int mod_id, float val);
char* get_mod_cst_label(int mod_id);
char* get_mod_cst_type(int mod_id);
float get_mod_cst_init_value(int mod_id);

#endif
