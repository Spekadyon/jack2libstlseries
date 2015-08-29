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

#include "jack2libstlseries.h"


typedef struct {
        jack_port_t *input_port;
} jack_data;

typedef struct {
        unsigned int sample_rate;
        jack_default_audio_sample_t *data;
        size_t size;
        size_t position;
} audio_data;

typedef struct {
        const char *progname;
} status_data;

typedef struct {
        pthread_mutex_t mutex;
        pthread_cond_t cond;
} memory_sync;

typedef struct {
        STLSERIES stlseries;
} keyboard_data;

typedef struct {
        jack_data jack;
        audio_data audio;
        status_data status;
        memory_sync memsync;
        keyboard_data kbd;
} J2STL;
// rename index to position


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


void jack_shutdown(void *arg)
{
        exit(EXIT_FAILURE);
}


int jack_process(jack_nframes_t nframes, void *arg)
{
	static int i;
	J2STL *j2stl;
        jack_default_audio_sample_t *jack_data;
        size_t available_frames;

	j2stl = (J2STL *)arg;
        jack_data = jack_port_get_buffer(j2stl->jack.input_port, nframes);

        if ( (j2stl->audio.size - j2stl->audio.position) < nframes)
                available_frames = j2stl->audio.size - j2stl->audio.position;
	else
                available_frames = nframes;

	pthread_mutex_lock(&j2stl->memsync.mutex);
        memcpy(j2stl->audio.data + j2stl->audio.position, jack_data, available_frames *
	       sizeof(jack_default_audio_sample_t));
        j2stl->audio.position += available_frames;

        if (j2stl->audio.size == j2stl->audio.position) {
		pthread_cond_signal(&j2stl->memsync.cond);
                j2stl->audio.position = 0;
	}

	pthread_mutex_unlock(&j2stl->memsync.mutex);

//	fprintf(stderr, "%4d - Processing frames...\n", i);
	i += 1;

	return 0;
}


void *fftw_thread(void *arg)
{
	int ret;
	char *wisdom_name = NULL;
	int wisdom_name_size;
	J2STL *j2stl;
	jack_default_audio_sample_t *left;
	fftw_complex *in, *out;
	static int i;
	size_t max_left;
	fftw_plan p;

	/* arg -> data cast */
	j2stl = (J2STL *)arg;

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
	wisdom_name_size = asprintf(&wisdom_name, "%s.wisdom",
				    j2stl->status.progname);
	if (wisdom_name_size == -1) {
		fprintf(stderr, "Unable to allocate memory for wisdow filename:"
				" %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	left = malloc(sizeof(jack_default_audio_sample_t) * j2stl->audio.size);
	if (left == NULL) {
		fprintf(stderr, "Unable to allocate memory for audio buffers "
				"(fftw thread): %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	in = fftw_malloc(sizeof(fftw_complex) * j2stl->audio.size);
	out = fftw_malloc(sizeof(fftw_complex) * j2stl->audio.size);
	if ( (in == NULL) || (out == NULL) ) {
		fprintf(stderr, "Unable to allocate memory for fftw\n");
		exit(EXIT_FAILURE);
	}

	if (fftw_import_wisdom_from_filename(wisdom_name) == 0)
		fprintf(stderr, "Unable to retrieve wisdom from %s\n",
			wisdom_name);
	p = fftw_plan_dft_1d(j2stl->audio.size, in, out, FFTW_FORWARD,
			     FFTW_PATIENT);
	if (fftw_export_wisdom_to_filename(wisdom_name) == 0)
		fprintf(stderr, "Unable to export wisdom to %s\n", wisdom_name);
	free(wisdom_name);
	wisdom_name_size = -1;

	while (1) {
		int low = 0, mid = 0, high = 0;

		pthread_mutex_lock(&j2stl->memsync.mutex);
		pthread_cond_wait(&j2stl->memsync.cond, &j2stl->memsync.mutex);
		memcpy(left, j2stl->audio.data,
		       sizeof(jack_default_audio_sample_t) * j2stl->audio.size);
		pthread_mutex_unlock(&j2stl->memsync.mutex);

		for (size_t i = 0; i < j2stl->audio.size; i++) {
			in[i] = left[i];
		}
		fftw_execute(p);
		max_left = 0;
		for (size_t i = 0; i < j2stl->audio.size/2; i++) {
			double freq = i * j2stl->audio.sample_rate /
				(double)(j2stl->audio.size - 1);
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
		ret = stlseries_setcolor_normal(j2stl->kbd.stlseries,
						STLSERIES_ZONE_LEFT,
						retrieve_color(cabs(out[low]) /
							(double)(j2stl->audio.size)),
						STLSERIES_SATURATION_HIGH);
		if (ret)
			fprintf(stderr, "Unable to set keyboard color "
					"(left)\n");
		ret = stlseries_setcolor_normal(j2stl->kbd.stlseries,
						STLSERIES_ZONE_CENTER,
						retrieve_color(cabs(out[mid]) /
							(double)(j2stl->audio.size)),
						STLSERIES_SATURATION_HIGH);
		if (ret)
			fprintf(stderr, "Unable to set keyboard color "
					"(center)\n");
		ret = stlseries_setcolor_normal(j2stl->kbd.stlseries,
						STLSERIES_ZONE_RIGHT,
						retrieve_color(cabs(out[high]) /
							(double)(j2stl->audio.size)),
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
	J2STL j2stl;
	pthread_t fftw_pthread_t;
	int ret;

	memset(&j2stl, 0, sizeof(audio_data));
	pthread_mutex_init(&j2stl.memsync.mutex, NULL);
	pthread_cond_init(&j2stl.memsync.cond, NULL);
	j2stl.status.progname = basename(argv[0]);

	/* Stlseries init */
	if (stlseries_open(&j2stl.kbd.stlseries)) {
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

        jack_set_process_callback(jack_client_ptr, jack_process, &j2stl);
        jack_on_shutdown(jack_client_ptr, jack_shutdown, NULL);

	j2stl.audio.sample_rate = jack_get_sample_rate(jack_client_ptr);
	j2stl.audio.size = lrint((SAMPLE_DURATION / 1000.0) *
				 j2stl.audio.sample_rate);
	j2stl.audio.data = malloc(sizeof(jack_default_audio_sample_t) *
				  j2stl.audio.size);
	if (j2stl.audio.data == NULL) {
		fprintf(stderr, "Unable to allocate memory for sound buffers: "
				"%s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "Sample rate : %u\n", j2stl.audio.sample_rate);

	j2stl.jack.input_port = jack_port_register(jack_client_ptr, "input",
					     JACK_DEFAULT_AUDIO_TYPE,
					     JackPortIsInput, 0);
	if (j2stl.jack.input_port == NULL) {
		fprintf(stderr, "No port available.\n");
		exit(EXIT_FAILURE);
	}

	/* Threads launch */

	if ((ret = pthread_create(&fftw_pthread_t, NULL,
				  fftw_thread, &j2stl))) {
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
	free(j2stl.audio.data);
	pthread_cond_destroy(&j2stl.memsync.cond);
	pthread_mutex_destroy(&j2stl.memsync.mutex);
	stlseries_close();

	return EXIT_SUCCESS;
}

