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

#define _GNU_SOURCE /* asprintf */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <complex.h>

#include <fftw3.h>
#include <jack/jack.h>
#include <stlseries.h>

#include "jack2libstlseries.h"


struct fftw_handle {
	jack_default_audio_sample_t *raw_data;
	fftw_complex *in;
	fftw_complex *out;
	fftw_plan p;
};


/*
 * Transform the amplitude in color
 * see jack2libstlseries.h for limit definitions
 */

static unsigned char retrieve_color(double amplitude)
{
	if (amplitude < AMP_OFF)
		return STLSERIES_COLOR_NONE;
	else if (amplitude < AMP_BLUE)
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


static void process_loop(J2STL *j2stl, struct fftw_handle *fftwp)
{
	int ret;
	/* Loop counter */
	static int loop_counter;
	/* Amplitude maxima */
	double bass = 0, medium = 0, treble = 0;

	/* Wait for data */
	pthread_mutex_lock(&j2stl->memsync.mutex);
	pthread_cond_wait(&j2stl->memsync.cond, &j2stl->memsync.mutex);

	/* Copy data to local buffer */
	memcpy(fftwp->raw_data, j2stl->audio.data,
	       sizeof(jack_default_audio_sample_t) * j2stl->audio.size);

	/* Unlock memory */
	pthread_mutex_unlock(&j2stl->memsync.mutex);

	/* jack_default_audio_sample_t to fftw_complex conversion */
	for (size_t i = 0; i < j2stl->audio.size; i++) {
		fftwp->in[i] = fftwp->raw_data[i];
	}

	/* FFTW plan execution */
	fftw_execute(fftwp->p);

	/* Transfer function calculations */
	for (size_t i = 0; i < j2stl->audio.size; i++) {
		double complex cur_bass, cur_medium, cur_treble;
		double freq = i * j2stl->audio.sample_rate /
			(double)(j2stl->audio.size - 1);

		cur_bass = fftwp->out[i]
			* 1.0 / (1.0 + (double)FREQ_LIMIT_BASS_LOW / freq)
			* 1.0 / (1.0 + freq / (double)FREQ_LIMIT_BASS_HIGH);
		cur_medium= fftwp->out[i]
			* 1.0 / (1.0 + (double)FREQ_LIMIT_MEDIUM_LOW / freq)
			* 1.0 / (1.0 + freq / (double)FREQ_LIMIT_MEDIUM_HIGH);
		cur_treble = fftwp->out[i]
			* 1.0 / (1.0 + (double)FREQ_LIMIT_TREBLE_LOW / freq)
			* 1.0 / (1.0 + freq / (double)FREQ_LIMIT_TREBLE_HIGH);

		if (cabs(cur_bass) > bass)
			bass = cabs(cur_bass);
		if (cabs(cur_medium) > medium)
			medium = cabs(cur_medium);
		if (cabs(cur_treble) > treble)
			treble = cabs(cur_treble);
	}

	/*
	 * Set keyboard color
	 */
	ret = stlseries_setcolor_normal(j2stl->kbd.stlseries,
					STLSERIES_ZONE_LEFT,
					retrieve_color(bass /
						       (double)(j2stl->audio.size)),
					STLSERIES_SATURATION_HIGH);
	if (ret)
		fprintf(stderr, "Unable to set keyboard color "
			"(left)\n");
	ret = stlseries_setcolor_normal(j2stl->kbd.stlseries,
					STLSERIES_ZONE_CENTER,
					retrieve_color(medium /
						       (double)(j2stl->audio.size)),
					STLSERIES_SATURATION_HIGH);
	if (ret)
		fprintf(stderr, "Unable to set keyboard color "
			"(center)\n");
	ret = stlseries_setcolor_normal(j2stl->kbd.stlseries,
					STLSERIES_ZONE_RIGHT,
					retrieve_color(treble /
						       (double)(j2stl->audio.size)),
					STLSERIES_SATURATION_HIGH);
	if (ret)
		fprintf(stderr, "Unable to set keyboard color "
			"(right)\n");

	loop_counter += 1;
}


/*
 * Worker thread
 * Retreive data from J2STL.audio structure, calculate the fast fourier
 * transform and extract the amplitude from the signals
 * Controls the keyboard color
 */

void *fftw_thread(void *arg)
{
	J2STL *j2stl;
	/* FFTW variables */
	struct fftw_handle fftwh;

	j2stl = (J2STL *)arg;

	if (j2stl->status.verbose) {
		fprintf(stderr, "FFTW thread launched\n");
		fprintf(stderr, "FFTW thread initialization\n");
	}

	/* Memory allocation */
	fftwh.raw_data = malloc(sizeof(jack_default_audio_sample_t) *
			   j2stl->audio.size);
	if (fftwh.raw_data == NULL) {
		fprintf(stderr, "Unable to allocate memory for audio buffers "
				"(fftw thread): %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	pthread_cleanup_push(free, fftwh.raw_data);

	fftwh.in = fftw_malloc(sizeof(fftw_complex) * j2stl->audio.size);
	fftwh.out = fftw_malloc(sizeof(fftw_complex) * j2stl->audio.size);
	if ( (fftwh.in == NULL) || (fftwh.out == NULL) ) {
		fprintf(stderr, "Unable to allocate memory for fftw\n");
		exit(EXIT_FAILURE);
	}
	pthread_cleanup_push(fftw_free, fftwh.in);
	pthread_cleanup_push(fftw_free, fftwh.out);

	/* Wisdom import */
	if (j2stl->status.wisdomfile) {
		if (j2stl->status.verbose)
			fprintf(stderr, "FFTW thread - loading wisdom from %s\n",
				j2stl->status.wisdomfile);
		if (!fftw_import_wisdom_from_filename(j2stl->status.wisdomfile))
			fprintf(stderr, "Unable to retrieve wisdom from %s\n",
				j2stl->status.wisdomfile);
	}

	/* Plan creation */
	if (j2stl->status.verbose)
		fprintf(stderr, "FFTW thread - plan creation\n");
	fftwh.p = fftw_plan_dft_1d(j2stl->audio.size, fftwh.in, fftwh.out,
				   FFTW_FORWARD, FFTW_PATIENT);
	if (j2stl->status.verbose)
		fprintf(stderr, "FFTW thread - plan creation: done\n");
	pthread_cleanup_push(fftw_destroy_plan, fftwh.p);

	/* Save wisdom */
	if (j2stl->status.wisdomfile) {
		if (j2stl->status.verbose)
			fprintf(stderr, "FFTW thread - writing wisdom to %s\n",
				j2stl->status.wisdomfile);
		if (!fftw_export_wisdom_to_filename(j2stl->status.wisdomfile))
			fprintf(stderr, "Unable to export wisdom to %s\n",
				j2stl->status.wisdomfile);
	}

	/* Process loop */
	if (j2stl->status.verbose)
		fprintf(stderr, "FFTW tread - main loop started\n");

	while (1) {
		process_loop(j2stl, &fftwh);
	}

	/* Should never get there */
	pthread_cleanup_pop(1); /* plan cleanup */
	pthread_cleanup_pop(1); /* fftw_free out */
	pthread_cleanup_pop(1); /* fftw_free in */
	pthread_cleanup_pop(1); /* free raw_data */

	return NULL;
}

