#include "synth.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

#define PCM_DEVICE "default"

#ifndef M_PI
#define M_PI 3.14159365359879323846
#define M_2PI 6.28318530718
#define M_PI2 1.57079632679
#endif
#ifndef alloca
#define alloca(x)  __builtin_alloca(x)
#endif


#ifdef DEBUG
#define DEBUG_VAL 1
#else
#define DEBUG_VAL 0
#endif

#define debug_print(fmt, ...) \
	do { if(DEBUG_VAL) printf(fmt, __VA_ARGS__); } while(0)

#define LINE_MAX_LEN 255
#define NANO 1000000000

unsigned int rate = 44100; // samples per second

snd_pcm_t *pcm_handle;
snd_pcm_hw_params_t *params;
snd_pcm_uframes_t frames;

/*************************/
typedef struct mod {
	char type[3];
	struct mod **inputs;
	int *input_idxs;
	float *outputs;
	void (*tick)(struct mod*);
	void *data;
} mod;

int nmods = 0;
mod *mods = NULL;
void init_mods(int n) {
	nmods = n;
	mods = malloc(n * sizeof(mod));
}
/*************************/


float get_input(mod *m, int i) { return m->inputs[i]->outputs[m->input_idxs[i]]; }

/* CONSTANT OUT VALUE */
typedef struct cst_data {
	char label[LINE_MAX_LEN];
	char type[3];
	float init_val;
	float val;
} cst_data;

#define CST_OUT_VAL 0
void cst_tick(mod *m) {
	m->outputs[CST_OUT_VAL] = ((cst_data*)m->data)->val;
}
int make_cst(mod *m) {
	memcpy(m->type, "CST", 3);
	m->outputs = malloc(sizeof(float));
	m->tick = &cst_tick;
	m->data = malloc(sizeof(cst_data));
	memset(m->data, 0, sizeof(cst_data));
	return 0;
}
void cst_set_val(mod *m, float val) {
	((cst_data*)m->data)->val = val;
}
void cst_set_init_val(mod *m, float val) {
	((cst_data*)m->data)->init_val = val;
	((cst_data*)m->data)->val = val;
}
void cst_set_type(mod *m, char *type) {
	strncpy(((cst_data*)m->data)->type, type, 3);
}
void cst_set_label(mod *m, char *label) {
	int pos = -1;

	while('\n' != label[++pos])
		((cst_data*)m->data)->label[pos] = label[pos];
}
void cst_print(mod *m) {
	cst_data *data =((cst_data*)m->data);
	printf("CST %f %c%c%c %s\n", 
			data->init_val,
			data->type[0], data->type[1], data->type[2], 
			data->label);
}
void set_mod_cst_value(int mod_id, float val) {
	cst_set_val(&mods[mod_id], val);
}
char* get_mod_cst_label(int mod_id) {
	return ((cst_data*)mods[mod_id].data)->label;
}
char* get_mod_cst_type(int mod_id) {
	return ((cst_data*)mods[mod_id].data)->type;
}
float get_mod_cst_init_value(int mod_id) {
	return ((cst_data*)mods[mod_id].data)->init_val;
}
char *get_mod_type(int mod_id) {
	return mods[mod_id].type;
}
int get_nmods() { return nmods; }

#define FAD_IN_SIG1 0
#define FAD_IN_SIG2 1
#define FAD_IN_MIX  2
#define FAD_OUT_VAL 0
void fad_tick(mod *m) {
	float s1 = get_input(m, FAD_IN_SIG1); 
	float s2 = get_input(m, FAD_IN_SIG2); 
	float mix = get_input(m, FAD_IN_MIX); 
	float out = s1 * (1 - mix) + s2 * mix;
	debug_print("FAD %p - %f, %f @ %f%% = %f\n", (void*)m, s1, s2, mix, out);
	m->outputs[FAD_OUT_VAL] = out;
}
int make_fad(mod *m) {
	memcpy(m->type, "FAD", 3);
	m->inputs = malloc(3 * sizeof(mod*));
	m->input_idxs = malloc(3 * sizeof(int));
	m->outputs = malloc(sizeof(float));
	m->tick = &fad_tick;
	return 0;
}

#define ADD_IN1 0
#define ADD_IN2 1
#define ADD_OUT_VAL 0
void add_tick(mod *m) {
	float a1 = get_input(m, ADD_IN1); 
	float a2 = get_input(m, ADD_IN2); 
	debug_print("ADD %p - %f + %f = %f\n", (void*)m, a1, a2, a1 + a2);
	m->outputs[ADD_OUT_VAL] = a1 + a2;
}
int make_add(mod *m) {
	memcpy(m->type, "ADD", 3);
	m->inputs = malloc(2 * sizeof(mod*));
	m->input_idxs = malloc(2 * sizeof(int));
	m->outputs = malloc(sizeof(float));
	m->tick = &add_tick;
	return 0;
}

#define OCC_IN_FREQ 0
#define OCC_OUT_SIN 0
#define OCC_OUT_TRI 1
#define OCC_OUT_SAW 2
#define OCC_OUT_SQU 3
void occ_tick(mod *m) { 
	float freq_in = get_input(m, OCC_IN_FREQ); 

	float phase = *(float*)m->data;
	phase += freq_in * M_2PI / (float)rate;
	phase += ((phase >= M_2PI) * -M_2PI) + ((phase < 0.0) * M_2PI);
	//printf("%f %f %f\n", freq_in, freq_in * M_2PI / (float)rate, phase);
	*(float*)m->data = phase;

	float sample_sin = 0.5 + 0.5 * sin(phase);
	float sample_tri = (
			((phase >= M_PI2 && phase < 3 * M_PI2) * (1. - (phase - M_PI2) / M_PI)) +
			((phase < M_PI2) * phase / M_PI2) +
			((phase >= 3 * M_PI2) * (phase - 3. * M_PI2) / M_PI2));
	float sample_saw = phase / M_2PI;
	float sample_squ = (phase >= M_PI2 && phase < 3 * M_PI2);

	//float sample = 0.5 + 0.5 * sin(theta * freq_in);
	debug_print("OCC %p - %f @ %f = sin %f, tri %f, saw %f, squ %f\n",
		 	(void*)m, phase, freq_in, sample_sin, sample_tri, sample_saw, sample_squ);
	m->outputs[OCC_OUT_SIN] = sample_sin;
	m->outputs[OCC_OUT_TRI] = sample_tri;
	m->outputs[OCC_OUT_SAW] = sample_saw;
	m->outputs[OCC_OUT_SQU] = sample_squ;
}
int make_occ(mod *m) {
	memcpy(m->type, "OCC", 3);
	m->inputs = malloc(sizeof(mod*));
	m->input_idxs = malloc(sizeof(int));
	m->outputs = malloc(4 * sizeof(float));
	m->data = malloc(sizeof(float));
	memset(m->data, 0, sizeof(float));
	m->tick = &occ_tick;
	return 0;
}

#define VCA_IN_CV 0
#define VCA_IN_SIG 1
#define VCA_OUT_SIG 0
void vca_tick(mod *m) {
	float in_cv = get_input(m, VCA_IN_CV);
	float in_sig = get_input(m, VCA_IN_SIG);

	debug_print("VCA %p - %f * %f = %f\n", (void*)m, in_cv, in_sig, in_cv * in_sig);
	m->outputs[VCA_OUT_SIG] = in_cv * in_sig;
}
int make_vca(mod *m) {
	memcpy(m->type, "VCA", 3);
	m->inputs = malloc(2 * sizeof(mod*));
	m->input_idxs = malloc(2 * sizeof(int));
	m->outputs = malloc(sizeof(float));
	m->tick = &vca_tick;
	return 0;
}

#define VCF_IN_CUT 0
#define VCF_IN_RES 1
#define VCF_IN_SIG 2
#define VCF_OUT_SIG 0
#define vcf_stages 4
typedef struct vcf_data {
	float sn[vcf_stages]; // s(n)
	float snm1[vcf_stages]; // s(n-1)
} vcf_data;
void vcf_tick(mod *m) {
	float cut = get_input(m, VCF_IN_CUT);
	float res = get_input(m, VCF_IN_RES);
	float sig = get_input(m, VCF_IN_SIG);
	vcf_data *data = (vcf_data*)m->data;

	float tmp;
	// Pull data from the last stage
	for(int i = vcf_stages -1; i > 0; i--) {
		tmp = data->sn[i];
		data->sn[i] = data->sn[i-1] * cut + data->snm1[i] * (1.0f - cut);
		data->snm1[i] = tmp;
	}
	tmp = data->sn[0];
	data->sn[0] = sig * cut + data->snm1[0] * (1.0f - cut) + data->snm1[vcf_stages-1] * res;

	debug_print("VCF cut %f, res %f, sig %f = %f\n", cut, res, sig,
			data->sn[vcf_stages -1]);

	m->outputs[VCF_OUT_SIG] = data->sn[vcf_stages -1];
}
int make_vcf(mod *m) {
	memcpy(m->type, "VCF", 3);
	m->inputs = malloc(3 * sizeof(mod*));
	m->input_idxs = malloc(3 * sizeof(int));
	m->outputs = malloc(sizeof(float));
	m->tick = &vcf_tick;
	m->data = (void*)malloc(sizeof(vcf_data));
	memset(m->data, 0, sizeof(vcf_data));

	return 0;
}

#define OTP_IN 0
typedef struct otp_data {
	float *fbuf;
	int16_t *ibuf;
	int i;
} otp_data;
void otp_tick(mod *m) {
	unsigned int pcm;
	float in = get_input(m, OTP_IN);
	otp_data *data = (otp_data*)m->data;
	data->fbuf[data->i++] = in;

	// Time to dump the data into the audio buffer?
	if(data->i == frames) {
		for(int i = 0; i < frames; i++) {
			data->ibuf[i] = (int16_t)(data->fbuf[i] * 32767.0f);
			//printf("%f -> %i\n", data->fbuf[i], data->ibuf[i]);
		}
		if ((pcm = snd_pcm_writei(pcm_handle, data->ibuf, frames)) == -EPIPE) {
			printf("XRUN.\n");
			snd_pcm_prepare(pcm_handle);
		} else if (pcm < 0) {
			printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(pcm));
		}
		data->i = 0;
	}
}
int make_otp(mod *m) {
	memcpy(m->type, "OTP", 3);
	m->inputs = malloc(sizeof(mod*));
	m->input_idxs = malloc(sizeof(int));
	m->outputs = NULL;
	m->tick = &otp_tick;
	otp_data *data = (otp_data*)malloc(sizeof(otp_data));
	data->fbuf = (float*)malloc(frames * sizeof(float));
	data->ibuf = (int16_t*)malloc(frames * sizeof(int));
	data->i = 0;
	m->data = (void*)data;
	return 0;
}

/*************************/
void freadline(char line[LINE_MAX_LEN], FILE *f) {
	while(isspace(fgetc(f)));
	if(feof(f)) return;
	fseek(f, -1, SEEK_CUR); 
	int i = 0;
	line[i] = fgetc(f);
	while(line[i] != '\n') 
		line[++i] = fgetc(f);
}
void parse_mod_line(mod *mods, char line[LINE_MAX_LEN]) {
	int i = 0;
	int n = atoi(line);
	while(isdigit(line[i++]));
	
	if(0 == strncmp("CST", &line[i], 3)) {
		make_cst(&mods[n]);
		i += 4;
		cst_set_val(&mods[n], atof(&line[i]));
		cst_set_init_val(&mods[n], atof(&line[i]));
		while(!isspace(line[i++]));

		if(0 == strncmp("HFO", &line[i], 3) ||
			 0 == strncmp("LFO", &line[i], 3) ||
			 0 == strncmp("PER", &line[i], 3) ||
		   0 == strncmp("NDS", &line[i], 3)) {
			cst_set_type(&mods[n], &line[i]);
			i += 4;
		}
		else 
			printf("Bad CST UI Type in: %s\n", line);

		cst_set_label(&mods[n], &line[i]);
		cst_print(&mods[n]);
	}
	else if(0 == strncmp("ADD", &line[i], 3)) {
		make_add(&mods[n]);
		i += 4;
		mods[n].inputs[ADD_IN1] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[ADD_IN1] = atoi(&line[i]);

		while(isdigit(line[i++]));

		mods[n].inputs[ADD_IN2] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[ADD_IN2] = atoi(&line[i]);
	}
	else if(0 == strncmp("FAD", &line[i], 3)) {
		make_fad(&mods[n]);
		i += 4;
		mods[n].inputs[FAD_IN_SIG1] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[FAD_IN_SIG1] = atoi(&line[i]);

		while(isdigit(line[i++]));

		mods[n].inputs[FAD_IN_SIG2] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[FAD_IN_SIG2] = atoi(&line[i]);

		while(isdigit(line[i++]));

		mods[n].inputs[FAD_IN_MIX] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[FAD_IN_MIX] = atoi(&line[i]);
	}
	else if(0 == strncmp("OCC", &line[i], 3)) {
		make_occ(&mods[n]);
		i += 4;
		mods[n].inputs[OCC_IN_FREQ] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[OCC_IN_FREQ] = atoi(&line[i]);
	}
	else if(0 == strncmp("VCA", &line[i], 3)) {
		make_vca(&mods[n]);
		i += 4;
		mods[n].inputs[VCA_IN_CV] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[VCA_IN_CV] = atoi(&line[i]);

		while(isdigit(line[i++]));

		mods[n].inputs[VCA_IN_SIG] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[VCA_IN_SIG] = atoi(&line[i]);
	}
	else if(0 == strncmp("VCF", &line[i], 3)) {
		make_vcf(&mods[n]);
		i += 4;
		mods[n].inputs[VCF_IN_CUT] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[VCF_IN_CUT] = atoi(&line[i]);

		while(isdigit(line[i++]));

		mods[n].inputs[VCF_IN_RES] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[VCF_IN_RES] = atoi(&line[i]);

		while(isdigit(line[i++]));

		mods[n].inputs[VCF_IN_SIG] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[VCF_IN_SIG] = atoi(&line[i]);
	}
	else if(0 == strncmp("OUT", &line[i], 3)) {
		make_otp(&mods[n]);
		i += 4;
		mods[n].inputs[OTP_IN] = &mods[atoi(&line[i])];
		while('/' != line[i++]);
		mods[n].input_idxs[OTP_IN] = atoi(&line[i]);
	}
	else
		printf("Bad module type in: %s\n", line);
}
int load_network(char *filename) {
	FILE * f = fopen(filename, "r");
	// Go to the last line and get the highest mod number.
	fseek(f, 1, SEEK_END);
	// Ignore whitespace at the end
	while(isspace(fgetc(f)))
		fseek(f, -2, SEEK_CUR); 
	// Move back past the \n
	fseek(f, -1, SEEK_CUR); 
	// Find the next carrige return
	while(fgetc(f) != '\n')
		fseek(f, -2, SEEK_CUR);
	// Read the last line
	char line[LINE_MAX_LEN] = {0,};
	fread(line, 1, LINE_MAX_LEN, f);
	// Get the number
	init_mods(atoi(line) + 1); // Number from 0
	printf("nmods : %i\n", nmods);

	rewind(f);
	while(!feof(f)) {
		memset(line, 0, LINE_MAX_LEN);
		freadline(line, f);
		printf("%s--\n", line);
		parse_mod_line(mods, line);
	}
	
	return 0;
}
/*************************/

/*  ^^^^ 0.0 -> 1.0+  ^^^^ vvvv -32767 -> 32767 vvvv */

void init_pcm() {
	unsigned int pcm, tmp;

	/* Open the PCM device in playback mode */
	if ((pcm = snd_pcm_open(&pcm_handle, PCM_DEVICE,
					SND_PCM_STREAM_PLAYBACK, 0)) < 0) 
		printf("ERROR: Can't open \"%s\" PCM device. %s\n",
				PCM_DEVICE, snd_strerror(pcm));

	/* Allocate parameters object and fill it with default values*/
	snd_pcm_hw_params_alloca(&params);
	snd_pcm_hw_params_any(pcm_handle, params);

	/* Set parameters */
	if ((pcm = snd_pcm_hw_params_set_access(pcm_handle, params,
					SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) 
		printf("ERROR: Can't set interleaved mode. %s\n", snd_strerror(pcm));

	if ((pcm = snd_pcm_hw_params_set_format(pcm_handle, params,
					SND_PCM_FORMAT_S16_LE)) < 0) 
		printf("ERROR: Can't set format. %s\n", snd_strerror(pcm));


	/* mono only */
	if ((pcm = snd_pcm_hw_params_set_channels(pcm_handle, params, 1)) < 0) 
		printf("ERROR: Can't set channels number. %s\n", snd_strerror(pcm));

	if ((pcm = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0)) < 0) 
		printf("ERROR: Can't set rate. %s\n", snd_strerror(pcm));

	/* Write parameters */
	if ((pcm = snd_pcm_hw_params(pcm_handle, params)) < 0)
		printf("ERROR: Can't set harware parameters. %s\n", snd_strerror(pcm));

	/* Resume information */
	printf("PCM Sample size %i bits\n", snd_pcm_hw_params_get_sbits(params));

	printf("PCM name: '%s'\n", snd_pcm_name(pcm_handle));

	printf("PCM state: %s\n", snd_pcm_state_name(snd_pcm_state(pcm_handle)));

	snd_pcm_hw_params_get_channels(params, &tmp);
	printf("channels: %i ", tmp);

	if (tmp == 1)
		printf("(mono)\n");
	else if (tmp == 2)
		printf("(stereo)\n");

	snd_pcm_hw_params_get_rate(params, &tmp, 0);
	printf("rate: %d bps\n", tmp);

	snd_pcm_hw_params_get_period_size(params, &frames, 0);
	unsigned int period_time;
	int dir;
	snd_pcm_hw_params_get_period_time(params, &period_time, &dir);
	printf("Need %lu frames in %ums\n", frames, period_time);
}

long long int timespec_to_nsecs(struct timespec *t) {
	return (long long int)t->tv_sec * (long long int)NANO +
	 	(long long int)t->tv_nsec;
}
void nsecs_to_timespec(long long int nsecs, struct timespec *t) {
	t->tv_sec = nsecs / NANO;
	t->tv_nsec = nsecs % NANO;
}
// When b > a both the seconds and nanoseconds part will be -ve
static inline void timespec_diff(struct timespec *a, struct timespec *b, struct timespec *result) {
	long long int result_nsecs = timespec_to_nsecs(a) - timespec_to_nsecs(b);
	nsecs_to_timespec(result_nsecs, result);
}

void *synth_main_loop(void *synth_data) {

	synth_thread_data *thread_data = (synth_thread_data*)synth_data;

	init_pcm();

	load_network("layout.dat");

	//setup_network();

	// Init complete, signal the UI thread
	char alive = 1;
	pthread_mutex_lock(&thread_data->alive_mtx);
 	thread_data->alive = alive;
	pthread_mutex_unlock(&thread_data->alive_mtx);

	uint32_t frames_calced = 0;
	float sound_secs;
	struct timespec start, now, elapsed, pause, sound;
	long int max_sync_diff = 0.1 * NANO;

	clock_gettime(CLOCK_REALTIME, &start);

	while(alive) {
	
		for(int i = 0; i < nmods; i++)
			mods[i].tick(&mods[i]);

		frames_calced++;
		clock_gettime(CLOCK_REALTIME, &now);
		timespec_diff(&now, &start, &elapsed);
		
		sound_secs = (float)frames_calced / (float)rate;
		sound.tv_sec = (time_t)sound_secs;
		sound.tv_nsec = (long)((sound_secs - floor(sound_secs)) * (float)NANO);
		timespec_diff(&sound, &elapsed, &pause);
		//printf("calced %fsecs of sound in %fsecs\n",
		//		sound_secs, (float)elapsed.tv_sec + (float)elapsed.tv_nsec / (float)NANO);

		if(pause.tv_sec > 3) {
			printf("Clock too far out of sync\n");
			abort();
		}
		if(pause.tv_nsec > max_sync_diff) {
			pause.tv_nsec -= max_sync_diff / 2;
			debug_print("Audio calculation too far ahead sleep for %li = %fs\n",
					pause.tv_nsec,
				 	(float)pause.tv_nsec / (float)NANO);
			nanosleep(&pause, NULL);
		}

		pthread_mutex_lock(&thread_data->alive_mtx);
		alive = thread_data->alive;
		pthread_mutex_unlock(&thread_data->alive_mtx);
	}
	printf("Closing synth\n");

	snd_pcm_drain(pcm_handle);
	snd_pcm_close(pcm_handle);

	printf("Exiting synth\n");
	return NULL;
}
