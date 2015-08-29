/*
 * Copyright 2015 - Geoffrey Brun <geoffrey+git@spekadyon.org>
 *
 * This file is part of jack2libstlseries.
 *
 * jack2libstlseries is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * jack2libstlseries is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * jack2libstlseries. If not, see <http://www.gnu.org/licenses/>.
 */

/* asprintf */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <complex.h>
#include <errno.h>
#include <libgen.h>

#include <jack/jack.h>
#include <stlseries.h>
#include <fftw3.h>

/* Sample size, in milliseconds */
#define SAMPLE_DURATION	50

/* Frequency limits */
#define FREQ_LIMIT_BASS_MEDIUM		250
#define FREQ_LIMIT_MEDIUM_TREBLE	2000

/* Colors (upper limits)*/
#define AMP_OFF		0.008
#define AMP_BLUE	0.01
#define AMP_SKY		0.025
#define AMP_GREEN	0.040
#define AMP_YELLOW	0.055
#define AMP_ORANGE	0.070
#define AMP_RED		0.085
#define AMP_PURPLE	0.1


struct audio_data_s {
	jack_port_t *input_port;
	unsigned int sample_rate;
	size_t size;
	size_t index;
	jack_default_audio_sample_t *data;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	char *progname;
	STLSERIES stlseries;
};

typedef struct audio_data_s audio_data;


void shutdown(void *arg)
{
	exit(EXIT_FAILURE);
}


unsigned char retrieve_color(double amplitude)
{
	if (amplitude < AMP_BLUE)
		return STLSERIES_COLOR_BLUE;
	else if (amplitude < AMP_SKY)
		return STLSERIES_COLOR_SKY;
	else if (amplitude < AMP_GREEN)
		return STLSERIES_COLOR_GREEN;
	else if (amplitude < AMP_YELLOW)
		return STLSERIES_COLOR_YELLOW;
	else if (amplitude < AMP_ORANGE)
		return STLSERIES_COLOR_ORANGE;
	else if (amplitude < AMP_RED)
		return STLSERIES_COLOR_RED;
	else if (amplitude < AMP_PURPLE)
		return STLSERIES_COLOR_PURPLE;
	else
		return STLSERIES_COLOR_WHITE;
}


int process(jack_nframes_t nframes, void *arg)
{
	static int i;
	audio_data *data;
	jack_default_audio_sample_t *left;
	size_t nb_frames;

	data = (audio_data *)arg;
	left = jack_port_get_buffer(data->input_port, nframes);

	if ( (data->size - data->index) < nframes)
		nb_frames = data->size - data->index;
	else
		nb_frames = nframes;

	pthread_mutex_lock(&data->mutex);
	memcpy(data->data + data->index, left, nb_frames *
	       sizeof(jack_default_audio_sample_t));
	data->index += nb_frames;

	if (data->size == data->index) {
		pthread_cond_signal(&data->cond);
		data->index = 0;
	}

	pthread_mutex_unlock(&data->mutex);

//	fprintf(stderr, "%4d - Processing frames...\n", i);
	i += 1;

	return 0;
}


void *fftw_thread(void *arg)
{
	int ret;
	char *wisdom_name = NULL;
	int wisdom_name_size;
	audio_data *data;
	jack_default_audio_sample_t *left;
	fftw_complex *in, *out;
	static int i;
	size_t max_left;
	fftw_plan p;

	/* arg -> data cast */
	data = (audio_data *)arg;

#ifdef _OUTPUT
	FILE *fp;
	if ( (fp = fopen("jack.output", "w")) == NULL) {
		fprintf(stderr, "Unable to open jack.output: %s\n",
			strerror(errno));
	}
	fprintf(fp, "Array size: %zd\n", data->size);
	fprintf(fp, "Sampling frequency (Hz): %u\n", data->sample_rate);
#endif /* _OUTPUT */

	/* Wisdom name */
	wisdom_name_size = asprintf(&wisdom_name, "%s.wisdom", data->progname);
	if (wisdom_name_size == -1) {
		fprintf(stderr, "Unable to allocate memory for wisdow filename:"
				" %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	left = malloc(sizeof(jack_default_audio_sample_t) * data->size);
	if (left == NULL) {
		fprintf(stderr, "Unable to allocate memory for audio buffers "
				"(fftw thread): %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	in = fftw_malloc(sizeof(fftw_complex) * data->size);
	out = fftw_malloc(sizeof(fftw_complex) * data->size);
	if ( (in == NULL) || (out == NULL) ) {
		fprintf(stderr, "Unable to allocate memory for fftw\n");
		exit(EXIT_FAILURE);
	}

	if (fftw_import_wisdom_from_filename(wisdom_name) == 0)
		fprintf(stderr, "Unable to retrieve wisdom from %s\n",
			wisdom_name);
	p = fftw_plan_dft_1d(data->size, in, out, FFTW_FORWARD, FFTW_PATIENT);
	if (fftw_export_wisdom_to_filename(wisdom_name) == 0)
		fprintf(stderr, "Unable to export wisdom to %s\n", wisdom_name);
	free(wisdom_name);
	wisdom_name_size = -1;

	while (1) {
		int low = 0, mid = 0, high = 0;

		pthread_mutex_lock(&data->mutex);
		pthread_cond_wait(&data->cond, &data->mutex);
		memcpy(left, data->data, sizeof(jack_default_audio_sample_t) *
		       data->size);
		pthread_mutex_unlock(&data->mutex);

		for (size_t i = 0; i < data->size; i++) {
			in[i] = left[i];
		}
		fftw_execute(p);
		max_left = 0;
		for (size_t i = 0; i < data->size/2; i++) {
			double freq = i * data->sample_rate /
				(double)(data->size - 1);
			if (freq < FREQ_LIMIT_BASS_MEDIUM) {
				if (cabs(out[i]) > cabs(out[low]))
					low = i;
			} else if (freq < FREQ_LIMIT_MEDIUM_TREBLE) {
				if (mid == 0)
					mid = i;
				if (cabs(out[i]) > cabs(out[mid]))
					mid = i;
			} else {
				if (high == 0)
					high = i;
				if (cabs(out[i]) > cabs(out[high]))
					high = i;
			}

			if (cabs(out[i]) > cabs(out[max_left])) {
				max_left = i;
			}
		}

		/*
		 * Keyoard color
		 */
		ret = stlseries_setcolor_normal(data->stlseries,
						STLSERIES_ZONE_LEFT,
						retrieve_color(cabs(out[low]) /
							(double)(data->size)),
						STLSERIES_SATURATION_HIGH);
		if (ret)
			fprintf(stderr, "Unable to set keyboard color "
					"(left)\n");
		ret = stlseries_setcolor_normal(data->stlseries,
						STLSERIES_ZONE_CENTER,
						retrieve_color(cabs(out[mid]) /
							(double)(data->size)),
						STLSERIES_SATURATION_HIGH);
		if (ret)
			fprintf(stderr, "Unable to set keyboard color "
					"(center)\n");
		ret = stlseries_setcolor_normal(data->stlseries,
						STLSERIES_ZONE_RIGHT,
						retrieve_color(cabs(out[high]) /
							(double)(data->size)),
						STLSERIES_SATURATION_HIGH);
		if (ret)
			fprintf(stderr, "Unable to set keyboard color "
					"(right)\n");

#ifdef _OUTPUT
		fprintf(fp, "%g\t%g\t%g\t%g\t%g\t%g\n",
			low * data->sample_rate / (double)(data->size - 1),
			cabs(out[low]) / (double)(data->size),
			mid * data->sample_rate / (double)(data->size - 1),
			cabs(out[mid]) / (double)(data->size),
			high * data->sample_rate / (double)(data->size - 1),
			cabs(out[high]) / (double)(data->size));
		fflush(fp);
#endif

//		fprintf(stderr, "%4d - fftw: max left %zd (%g Hz)\n",
//				i, max_left, max_left * data->sample_rate / (double)(data->size - 1));
		i += 1;
	}

#ifdef _OUTPUT
	fclose(fp);
#endif

	fftw_destroy_plan(p);
	fftw_free(out);
	fftw_free(in);
	free(left);

	return NULL;
}

int main(int argc, char *argv[])
{
	const char client_name[] = "SteelSeries Sound Illuminator";
	const char *client_name_real;
	jack_client_t *jack_client_ptr;
	jack_status_t status;
	audio_data data;
	pthread_t fftw_pthread_t;
	int ret;

	memset(&data, 0, sizeof(audio_data));
	pthread_mutex_init(&data.mutex, NULL);
	pthread_cond_init(&data.cond, NULL);
	data.progname = basename(argv[0]);

	/* Stlseries init */
	if (stlseries_open(&data.stlseries)) {
		fprintf(stderr, "Unable to open steelseries keyboard.\n");
		exit(EXIT_FAILURE);
	}

	/* Jack init */

	jack_client_ptr = jack_client_open(client_name, JackNoStartServer,
					   &status, NULL);
	if (jack_client_ptr == NULL) {
		fprintf(stderr, "Unable to create jack client, status = "
				"0x%2.0x<\n", status);
		if (status & JackServerFailed)
			fprintf(stderr, "Unable to establish a connection to "
					"the server\n");
		exit(EXIT_FAILURE);
	}
	if (status & JackNameNotUnique) {
		client_name_real = jack_get_client_name(jack_client_ptr);
		fprintf(stderr, "Name assigned to the client: %s\n",
			client_name_real);
	} else {
		client_name_real = client_name;
	}

	jack_set_process_callback(jack_client_ptr, process, &data);
	jack_on_shutdown(jack_client_ptr, shutdown, NULL);

	data.sample_rate = jack_get_sample_rate(jack_client_ptr);
	data.size = lrint((SAMPLE_DURATION / 1000.0) * data.sample_rate);
	data.data = malloc(sizeof(jack_default_audio_sample_t) * data.size);
	if (data.data == NULL) {
		fprintf(stderr, "Unable to allocate memory for sound buffers: "
				"%s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "Sample rate : %u\n", data.sample_rate);

	data.input_port = jack_port_register(jack_client_ptr, "input",
					     JACK_DEFAULT_AUDIO_TYPE,
					     JackPortIsInput, 0);
	if (data.input_port == NULL) {
		fprintf(stderr, "No port available.\n");
		exit(EXIT_FAILURE);
	}

	/* Threads launch */

	if ((ret = pthread_create(&fftw_pthread_t, NULL, fftw_thread, &data))) {
		fprintf(stderr, "Unable to spawn fftw thread: %s\n",
			strerror(ret));
		exit(EXIT_FAILURE);
	}

	if (jack_activate(jack_client_ptr) != 0) {
		fprintf(stderr, "Unable to activate client\n");
		exit(EXIT_FAILURE);
	}

//	ports = jack_get_ports(jack_client_ptr, NULL, NULL, JackPortIsPhysical | JackPortIsInput);
//	if (ports == NULL) {
//		fprintf(stderr, "No physical capture ports");
//		exit(EXIT_FAILURE);
//	}
//	if (jack_connect(jack_client_ptr, ports[0], jack_port_name(data.input_port_left))) {
//		fprintf(stderr, "Cannot connect input ports\n");
//	}
//	if (jack_connect(jack_client_ptr, ports[1], jack_port_name(data.input_port_right))) {
//		fprintf(stderr, "Cannot connect input ports\n");
//	}
//	free(ports);


	sleep(600);
	

	jack_client_close(jack_client_ptr);
	free(data.data);
	pthread_cond_destroy(&data.cond);
	pthread_mutex_destroy(&data.mutex);
	stlseries_close();

	return EXIT_SUCCESS;
}

