#include "synth.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#define PCM_DEVICE "default"

#ifndef M_PI
#define M_PI 3.1415f
#endif
#ifndef alloca
#define alloca(x)  __builtin_alloca(x)
#endif

unsigned int rate = 44100; // samples per second
float theta = 0; // angle in radians at time t - convert seconds to Hz

snd_pcm_t *pcm_handle;
snd_pcm_hw_params_t *params;
snd_pcm_uframes_t frames;

#define NOT_READY 0
#define READY 1
#define DONE 2

typedef struct mod {
	struct mod **inputs;
	int *input_idxs;
	float *outputs;
	void (*tick)(struct mod*, float);
	void *data;
} mod;

float get_input(mod *m, int i) { return m->inputs[i]->outputs[m->input_idxs[i]]; }

/* CONSTANT OUT VALUE */
#define CST_OUT_VAL 0
void cst_tick(mod *m, float _) {
	m->outputs[CST_OUT_VAL] = *(float*)m->data;
}
int make_cst(mod *m) {
	m->outputs = malloc(sizeof(float));
	m->tick = &cst_tick;
	m->data = malloc(sizeof(float));
	return 0;
}
void cst_set_val(mod *m, float val) {
	*(float*)m->data = val;
}

#define OCC_IN_FREQ 0
#define OCC_OUT_SIN 0
#define OCC_OUT_TRI 1
#define OCC_OUT_SAW 2
#define OCC_OUT_SQU 3
void occ_tick(mod *m, float theta) { 
	float freq_in = get_input(m, OCC_IN_FREQ); 
	m->outputs[OCC_OUT_SIN] = 0.5 + 0.5 * sin(theta * freq_in);
}
int make_occ(mod *m) {
	m->inputs = malloc(sizeof(mod*));
	m->input_idxs = malloc(sizeof(int));
	m->outputs = malloc(4 * sizeof(float));
	m->tick = &occ_tick;
	return 0;
}

#define VCA_IN_CV 0
#define VCA_IN_SIG 1
#define VCA_OUT_SIG 0
void vca_tick(mod *m, float theta) {
	float in_cv = get_input(m, VCA_IN_CV);
	float in_sig = get_input(m, VCA_IN_SIG);

	m->outputs[VCA_OUT_SIG] = in_cv * in_sig;
}
int make_vca(mod *m) {
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
void vcf_tick(mod *m, float theta) {
	float cut = get_input(m, VCF_IN_CUT);
	float res = get_input(m, VCF_IN_RES);
	float sig = get_input(m, VCF_IN_SIG);
	vcf_data *data = (vcf_data*)m->data;

	//printf("cut %f, res %f, sig %f\n", cut, res, sig);

	float tmp;
	// Pull data from the last stage
	for(int i = vcf_stages -1; i > 0; i--) {
		tmp = data->sn[i];
		data->sn[i] = data->sn[i-1] * cut + data->snm1[i] * (1.0f - cut);
		data->snm1[i] = tmp;
	}
	tmp = data->sn[0];
	data->sn[0] = sig * cut + data->snm1[0] * (1.0f - cut) + data->snm1[vcf_stages-1] * res;

	m->outputs[VCF_OUT_SIG] = data->sn[vcf_stages -1];
}
int make_vcf(mod *m) {
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
void otp_tick(mod *m, float theta) {
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

static const int nmods = 11;
static mod *mods = NULL;

void setup_network() {

	if(mods) return;
	mods = malloc(nmods * sizeof(mod));

	/* Tone occilator */
	make_cst(&mods[0]);
	make_occ(&mods[1]);

	cst_set_val(&mods[0], 440.0f);
	mods[1].inputs[OCC_IN_FREQ] = &mods[0];
	mods[1].input_idxs[OCC_IN_FREQ] = CST_OUT_VAL;

	/* VCA control occilator */
	make_cst(&mods[2]);
	make_occ(&mods[3]);

	cst_set_val(&mods[2], 10.0f);
	mods[3].inputs[OCC_IN_FREQ] = &mods[2];
	mods[3].input_idxs[OCC_IN_FREQ] = CST_OUT_VAL;

	/* VCA */
	make_vca(&mods[4]);
	mods[4].inputs[VCA_IN_CV] = &mods[3];
	mods[4].input_idxs[VCA_IN_CV] = OCC_OUT_SIN;

	mods[4].inputs[VCA_IN_SIG] = &mods[1];
	mods[4].input_idxs[VCA_IN_SIG] = OCC_OUT_SIN;

	/* VCF control occilator */
	make_cst(&mods[5]);
	make_occ(&mods[6]);

	cst_set_val(&mods[5], 1.0f);
	mods[6].inputs[OCC_IN_FREQ] = &mods[5];
	mods[6].input_idxs[OCC_IN_FREQ] = CST_OUT_VAL;

	/* VCF */
	make_cst(&mods[7]);
	make_cst(&mods[8]);
	make_vcf(&mods[9]);

	cst_set_val(&mods[7], 0.25f);
	cst_set_val(&mods[8], 0.0f);
	mods[9].inputs[VCF_IN_CUT] = &mods[6];
	mods[9].input_idxs[VCF_IN_CUT] = OCC_OUT_SIN;
	//mods[9].inputs[VCF_IN_CUT] = &mods[7];
	//mods[9].input_idxs[VCF_IN_CUT] = CST_OUT_VAL;

	mods[9].inputs[VCF_IN_RES] = &mods[8];
	mods[9].input_idxs[VCF_IN_RES] = CST_OUT_VAL;

	mods[9].inputs[VCF_IN_SIG] = &mods[4];
	mods[9].input_idxs[VCF_IN_SIG] = VCA_OUT_SIG;

	/* OUTPUT */
	make_otp(&mods[10]);
	mods[10].inputs[OTP_IN] = &mods[9];
	mods[10].input_idxs[OTP_IN] = VCF_OUT_SIG;
}

void set_mod_value(int mod_id, float val) {
	cst_set_val(&mods[mod_id], val);
}

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
}

// NB does not work when b > a, it needs to underflow into the second part.
static inline void timespec_diff(struct timespec *a, struct timespec *b, struct timespec *result) {
    result->tv_sec = a->tv_sec - b->tv_sec;
    result->tv_nsec = a->tv_nsec - b->tv_nsec;
}

void *synth_main_loop(void *synth_data) {

	synth_thread_data *thread_data = (synth_thread_data*)synth_data;
	pthread_mutex_lock(&thread_data->alive_mtx);
	char alive = thread_data->alive;
	pthread_mutex_unlock(&thread_data->alive_mtx);

	init_pcm();

	setup_network();
	uint32_t frames_calced = 0;
	float sound_secs;
	struct timespec start, now, elapsed, pause, sound;
	long nano = 1000000000;
	long max_sync_diff = 0.25 * nano;

	clock_gettime(CLOCK_REALTIME, &start);

	while(alive) {
	
		for(int i = 0; i < nmods; i++)
			mods[i].tick(&mods[i], theta);
		theta += M_PI * 1.0 / rate;

		frames_calced++;
		clock_gettime(CLOCK_REALTIME, &now);
		timespec_diff(&now, &start, &elapsed);
		
		sound_secs = (float)frames_calced / (float)rate;
		sound.tv_sec = (time_t)sound_secs;
		sound.tv_nsec = (long)((sound_secs - floor(sound_secs)) * (float)nano);
		timespec_diff(&sound, &elapsed, &pause);

		if(pause.tv_sec > 0) {
			printf("Clock too far out of sync\n");
			abort();
		}
		if(pause.tv_nsec > max_sync_diff) {
			pause.tv_nsec -= max_sync_diff / 2;
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
