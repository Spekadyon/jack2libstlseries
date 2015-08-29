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

#define _POSIX_C_SOURCE 2

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


/*
 * Print help and exists
 */

void exit_help(const char *name)
{
	fprintf(stderr, "jack2libstlseries\n\n");
	fprintf(stderr, "Usage\n");
	fprintf(stderr, "\t%s [options]\n\n", name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-w wisdom_file\n");
	fprintf(stderr, "\t-t int, process timeout (default infinite)\n");
	fprintf(stderr, "\t-s int, audio sampling, in milliseconds "
			"(default %d)\n", SAMPLE_DURATION);
	fprintf(stderr, "\t-h print this message and exits\n");

	exit(EXIT_FAILURE);
}


/*
 * FFTW thread shutdown and memory cleaning
 */

void process_cleanup(J2STL *j2stl) {
	int ret;
	void *res;

	/* Threads termination */
	if (j2stl->status.verbose)
		fprintf(stderr, "Threads termination\n");
	ret = pthread_cancel(j2stl->status.fftw_thread);
	if (ret) {
		fprintf(stderr, "Unable to cancel fftw_thead: %s\n",
			strerror(ret));
		exit(EXIT_FAILURE);
	}
	ret = pthread_join(j2stl->status.fftw_thread, &res);
	if (ret) {
		fprintf(stderr, "Unable to join fftw thread: %s\n",
			strerror(ret));
		exit(EXIT_FAILURE);
	}
	if (res != PTHREAD_CANCELED) {
		fprintf(stderr, "fftw thread cancel() failed\n");
		exit(EXIT_FAILURE);
	}

	jack_client_close(j2stl->jack.client);

	/* Free memory & library close() */
	if (j2stl->status.verbose)
		fprintf(stderr, "Memory cleanup\n");
	free(j2stl->audio.data);
	pthread_cond_destroy(&j2stl->memsync.cond);
	pthread_mutex_destroy(&j2stl->memsync.mutex);
	stlseries_close();
}


/*
 * Executes this function when the client is disconnected by jack
 */

void jack_shutdown(void *arg)
{
	process_cleanup((J2STL *)arg);

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

/*
 * String to integer conversion
 * Exit on failure
 */

long int strtolint(char opt, const char *str)
{
	long int val;
	char *ptr;

	errno = 0;
	val = strtol(str, &ptr, 0);
	if (errno) {
		fprintf(stderr, "Option '-%c' - Unable to convert %s to an "
			"integer: %s\n", opt, optarg,
			strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (ptr == str) {
		fprintf(stderr, "Option '-%c' - Invalid option: %s\n",
			opt, optarg);
		exit(EXIT_FAILURE);
	}

	return val;
}


/*
 * Option parsing procedure
 */

int opt_parse(int argc, char * const * argv, status_data *status)
{
	int opt;
	const char opt_list[] = "w:vt:s:h";
	/* ajouter dump */

	while ( (opt = getopt(argc, argv, opt_list)) != -1 ) {
		switch (opt) {
		case 'h':
			exit_help(argv[0]);
			break;
		case 'w':
			status->wisdomfile = optarg;
			break;
		case 'v':
			status->verbose = 1;
			break;
		case 't':
			status->timeout = strtolint('t', optarg);
			if (status->timeout < 0) {
				fprintf(stderr, "Timeout must be positive\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 's':
			status->sampling = strtolint('s', optarg);
			if (status->sampling <= 0) {
				fprintf(stderr, "Sample duration must be "
					"positive\n");
				exit(EXIT_FAILURE);
			}
			break;
		}
	}

	if (!status->sampling)
		status->sampling = SAMPLE_DURATION;

	return 0;
}


/*
 * Option structure dump
 */

void opt_dump(const status_data *status)
{
	fprintf(stderr, "Option dump\n");
	fprintf(stderr, "\tWisdom file: %s\n", status->wisdomfile);
	fprintf(stderr, "\tVerbose: %d\n", status->verbose);
	fprintf(stderr, "\tTimeout: %d s\n", status->timeout);
	fprintf(stderr, "\tSample duration: %d ms\n", status->sampling);
}


int main(int argc, char *argv[])
{
	const char client_name[] = "SteelSeries Sound Illuminator";
	const char *client_name_real;
	jack_status_t status;
	J2STL j2stl;
	int ret;

	/* Memory initialization */
	memset(&j2stl, 0, sizeof(j2stl));
	pthread_mutex_init(&j2stl.memsync.mutex, NULL);
	pthread_cond_init(&j2stl.memsync.cond, NULL);
	j2stl.status.progname = basename(argv[0]);

	/* Options parsing */
	opt_parse(argc, argv, &j2stl.status);
	if (j2stl.status.verbose)
		opt_dump(&j2stl.status);


	/* Stlseries init */
	if (stlseries_open(&j2stl.kbd.stlseries)) {
		fprintf(stderr, "Unable to open steelseries keyboard.\n");
		exit(EXIT_FAILURE);
	}

	/* Jack init */
	j2stl.jack.client = jack_client_open(client_name, JackNoStartServer,
					   &status, NULL);
	if (j2stl.jack.client == NULL) {
		fprintf(stderr, "Unable to create jack client, status = "
				"0x%2.0x<\n", status);
		if (status & JackServerFailed)
			fprintf(stderr, "Unable to establish a connection to "
					"the server\n");
		exit(EXIT_FAILURE);
	}
	if (status & JackNameNotUnique) {
		client_name_real = jack_get_client_name(j2stl.jack.client);
		fprintf(stderr, "Name assigned to the client: %s\n",
			client_name_real);
	} else {
		client_name_real = client_name;
		if (j2stl.status.verbose)
			fprintf(stderr, "Jack client name: %s\n",
				client_name_real);
	}

	/* Set jack callbacks */
	jack_set_process_callback(j2stl.jack.client, jack_process, &j2stl);
	jack_on_shutdown(j2stl.jack.client, jack_shutdown, NULL);

	/* Retrieve audio format from jack */
	j2stl.audio.sample_rate = jack_get_sample_rate(j2stl.jack.client);
	j2stl.audio.size = lrint((j2stl.status.sampling / 1000.0) *
				 j2stl.audio.sample_rate);
	j2stl.audio.data = malloc(sizeof(jack_default_audio_sample_t) *
				  j2stl.audio.size);
	if (j2stl.audio.data == NULL) {
		fprintf(stderr, "Unable to allocate memory for sound buffers: "
				"%s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (j2stl.status.verbose)
		fprintf(stderr, "Jack sample rate: %u\n",
			j2stl.audio.sample_rate);

	/* jack: port creation */
	j2stl.jack.input_port = jack_port_register(j2stl.jack.client, "input",
					     JACK_DEFAULT_AUDIO_TYPE,
					     JackPortIsInput, 0);
	if (j2stl.jack.input_port == NULL) {
		fprintf(stderr, "No port available.\n");
		exit(EXIT_FAILURE);
	}

	/* Threads launch */
	if ((ret = pthread_create(&j2stl.status.fftw_thread, NULL,
				  fftw_thread, &j2stl))) {
		fprintf(stderr, "Unable to spawn fftw thread: %s\n",
			strerror(ret));
		exit(EXIT_FAILURE);
	}

	if (jack_activate(j2stl.jack.client) != 0) {
		fprintf(stderr, "Unable to activate client\n");
		exit(EXIT_FAILURE);
	}
	if (j2stl.status.verbose)
		fprintf(stderr, "Jack client thread launched\n");

	if (j2stl.status.timeout)
		sleep(j2stl.status.timeout);
	else
		for(;;);

	process_cleanup(&j2stl);

	return EXIT_SUCCESS;
}

