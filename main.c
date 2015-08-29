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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> //sleep()
#include <pthread.h>
#include <math.h>
#include <complex.h>
#include <errno.h>
#include <libgen.h>

#include <jack/jack.h>
#include <stlseries.h>
#include <fftw3.h>

#include "jack2libstlseries.h"


/*
 * Executes this function when the client is disconnected by jack
 */

void jack_shutdown(void *arg)
{
	J2STL *j2stl = (J2STL *)arg;

	free(j2stl->audio.data);
	pthread_cond_destroy(&j2stl->memsync.cond);
	pthread_mutex_destroy(&j2stl->memsync.mutex);
	stlseries_close();

	exit(EXIT_FAILURE);
}


/*
 * Procedure called by jack thread, copy audio in J2STL.audio.data buffer
 * Unlock mutex when the buffer is full.
 * Extra frames are discarded.
 */

int jack_process(jack_nframes_t nframes, void *arg)
{
	static int call_counter;
	J2STL *j2stl;
	jack_default_audio_sample_t *jack_data;
	size_t available_frames;

	j2stl = (J2STL *)arg;

	/* Retrieve data from jack, size == nframes */
	jack_data = jack_port_get_buffer(j2stl->jack.input_port, nframes);
	if (jack_data == NULL) {
		fprintf(stderr, "Invalid pointer returned by "
				"jack_port_get_buffer\n");
		exit(EXIT_FAILURE);
	}

	/* Check the available space in j2stl->audio.data */
	if ( (j2stl->audio.size - j2stl->audio.position) < nframes)
		available_frames = j2stl->audio.size - j2stl->audio.position;
	else
		available_frames = nframes;

	/* Lock memory before copy */
	pthread_mutex_lock(&j2stl->memsync.mutex);
	/* Copy into shared buffer */
	memcpy(j2stl->audio.data + j2stl->audio.position, jack_data,
	       available_frames * sizeof(jack_default_audio_sample_t));
	j2stl->audio.position += available_frames;

	/* If buffer full, signal ``processing'' thread */
	if (j2stl->audio.size == j2stl->audio.position) {
		pthread_cond_signal(&j2stl->memsync.cond);
		j2stl->audio.position = 0;
	}

	/* Unlock memory */
	pthread_mutex_unlock(&j2stl->memsync.mutex);

	call_counter += 1;

	return 0;
}


int main(int argc, char *argv[])
{
	const char client_name[] = "SteelSeries Sound Illuminator";
	const char *client_name_real;
	jack_client_t *jack_client_ptr;
	jack_status_t status;
	J2STL j2stl;
	pthread_t fftw_pthread_t;
	void *res;
	int ret;

	/* Memory initialization */
	memset(&j2stl, 0, sizeof(j2stl));
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

	/* Set jack callbacks */
	jack_set_process_callback(jack_client_ptr, jack_process, &j2stl);
	jack_on_shutdown(jack_client_ptr, jack_shutdown, NULL);

	/* Retrieve audio format from jack */
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

	/* jack: port creation */
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

	sleep(600);
	
	/* Threads termination */
	ret = pthread_cancel(fftw_pthread_t);
	if (ret) {
		fprintf(stderr, "Unable to cancel fftw_thead: %s\n",
			strerror(ret));
		exit(EXIT_FAILURE);
	}
	ret = pthread_join(fftw_pthread_t, &res);
	if (ret) {
		fprintf(stderr, "Unable to join fftw thread: %s\n",
			strerror(ret));
		exit(EXIT_FAILURE);
	}
	if (res != PTHREAD_CANCELED) {
		fprintf(stderr, "fftw thread cancel() failed\n");
		exit(EXIT_FAILURE);
	}

	jack_client_close(jack_client_ptr);

	/* Free memory & library close() */
	free(j2stl.audio.data);
	pthread_cond_destroy(&j2stl.memsync.cond);
	pthread_mutex_destroy(&j2stl.memsync.mutex);
	stlseries_close();

	return EXIT_SUCCESS;
}

