#include <alsa/asoundlib.h>
#include <stdio.h>
#include <math.h>

#define PCM_DEVICE "default"

#ifndef M_PI
#define M_PI 3.1415f
#endif

int rate; // samples per second
float theta; // angle in radians at time t - convert seconds to Hz
int buffer_size; // use a constant buffer size and sample rate across everything

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
	m->outputs[OCC_OUT_SIN] = sin(theta * freq_in);
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
#define VCA_OUT_SIG 2
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

/*************************/

int fill_buffer(float *buf) {
	mod mods[5];
	/* Tone occilator */
	make_cst(&mods[0]);
	make_occ(&mods[1]);

	cst_set_val(&mods[0], 440.0f);
	mods[1].inputs[OCC_IN_FREQ] = &mods[0];
	mods[1].input_idxs[OCC_IN_FREQ] = CST_OUT_VAL;

	/* control occilator */
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


	for(int i = 0; i < buffer_size; i++) {
		for(int mi = 0; mi < 5; mi++)
			mods[mi].tick(&mods[mi], theta);
		//buf[i] = mods[3].outputs[OCC_OUT_SIN];
		buf[i] = mods[4].outputs[VCA_OUT_SIG];
		theta += M_PI * 1.0 / rate;
	}
	return 0;
}

/*  ^^^^ 0.0 -> 1.0+  ^^^^ vvvv -32767 -> 32767 vvvv */

void to_sound(int16_t *ibuf, float *fbuf) {
	for(int i = 0; i < buffer_size; i++) {
		ibuf[i] = (int16_t)(fbuf[i] * 32767.0f);
	}
}

/*  ^^^^ 0.0 -> 1.0+  ^^^^ vvvv -32767 -> 32767 vvvv */

int main(int argc, char **argv) {
	unsigned int pcm, tmp, dir;
	int channels, seconds;
	snd_pcm_t *pcm_handle;
	snd_pcm_hw_params_t *params;
	snd_pcm_uframes_t frames;
	int16_t *buff;
	int loops;

	if (argc < 3) {
		printf("Usage: %s <sample_rate> <seconds>\n",
								argv[0]);
		return -1;
	}

	rate 	 = atoi(argv[1]);
	channels = 1;// atoi(argv[2]);
	seconds  = atoi(argv[2]);

	/* Open the PCM device in playback mode */
	if (pcm = snd_pcm_open(&pcm_handle, PCM_DEVICE,
					SND_PCM_STREAM_PLAYBACK, 0) < 0) 
		printf("ERROR: Can't open \"%s\" PCM device. %s\n",
					PCM_DEVICE, snd_strerror(pcm));

	/* Allocate parameters object and fill it with default values*/
	snd_pcm_hw_params_alloca(&params);

	snd_pcm_hw_params_any(pcm_handle, params);

	/* Set parameters */
	if (pcm = snd_pcm_hw_params_set_access(pcm_handle, params,
					SND_PCM_ACCESS_RW_INTERLEAVED) < 0) 
		printf("ERROR: Can't set interleaved mode. %s\n", snd_strerror(pcm));

	if (pcm = snd_pcm_hw_params_set_format(pcm_handle, params,
						SND_PCM_FORMAT_S16_LE) < 0) 
		printf("ERROR: Can't set format. %s\n", snd_strerror(pcm));


	if (pcm = snd_pcm_hw_params_set_channels(pcm_handle, params, channels) < 0) 
		printf("ERROR: Can't set channels number. %s\n", snd_strerror(pcm));

	if (pcm = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0) < 0) 
		printf("ERROR: Can't set rate. %s\n", snd_strerror(pcm));

	/* Write parameters */
	if (pcm = snd_pcm_hw_params(pcm_handle, params) < 0)
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

	printf("seconds: %d\n", seconds);	

	/* Allocate buffer to hold single period */
	snd_pcm_hw_params_get_period_size(params, &frames, 0);

	buffer_size = frames * channels /* 2 -> sample size */;
	buff = (int16_t *) malloc(buffer_size * sizeof(int16_t));

	float *fbuf = (float *)malloc(buffer_size * sizeof(float));

	snd_pcm_hw_params_get_period_time(params, &tmp, NULL);

	theta = 0;
	for (loops = (seconds * 1000000) / tmp; loops > 0; loops--) {

		fill_buffer(fbuf);
		to_sound(buff, fbuf);
		

		if (pcm = snd_pcm_writei(pcm_handle, buff, frames) == -EPIPE) {
			printf("XRUN.\n");
			snd_pcm_prepare(pcm_handle);
		} else if (pcm < 0) {
			printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(pcm));
		}

	}

	snd_pcm_drain(pcm_handle);
	snd_pcm_close(pcm_handle);
	free(buff);

	return 0;
}
