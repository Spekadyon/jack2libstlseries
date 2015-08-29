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


static void process_loop(J2STL *j2stl, struct fftw_handle *fftwp)
{
	int ret;
	/* Local raw audio data */
	size_t max_left;
	/* Loop counter */
	static int loop_counter;
	/* Maximums */
	int low = 0, mid = 0, high = 0;

	pthread_mutex_lock(&j2stl->memsync.mutex);
	pthread_cond_wait(&j2stl->memsync.cond, &j2stl->memsync.mutex);
	memcpy(fftwp->raw_data, j2stl->audio.data,
	       sizeof(jack_default_audio_sample_t) * j2stl->audio.size);
	pthread_mutex_unlock(&j2stl->memsync.mutex);

	for (size_t i = 0; i < j2stl->audio.size; i++) {
		fftwp->in[i] = fftwp->raw_data[i];
	}
	fftw_execute(fftwp->p);
	max_left = 0;
	for (size_t i = 0; i < j2stl->audio.size/2; i++) {
		double freq = i * j2stl->audio.sample_rate /
			(double)(j2stl->audio.size - 1);
		if (freq < FREQ_LIMIT_BASS_MEDIUM) {
			if (cabs(fftwp->out[i]) > cabs(fftwp->out[low]))
				low = i;
		} else if (freq < FREQ_LIMIT_MEDIUM_TREBLE) {
			if (mid == 0)
				mid = i;
			if (cabs(fftwp->out[i]) > cabs(fftwp->out[mid]))
				mid = i;
		} else {
			if (high == 0)
				high = i;
			if (cabs(fftwp->out[i]) > cabs(fftwp->out[high]))
				high = i;
		}

		if (cabs(fftwp->out[i]) > cabs(fftwp->out[max_left])) {
			max_left = i;
		}
	}

	/*
	 * Set keyboard color
	 */
	ret = stlseries_setcolor_normal(j2stl->kbd.stlseries,
					STLSERIES_ZONE_LEFT,
					retrieve_color(cabs(fftwp->out[low]) /
						       (double)(j2stl->audio.size)),
					STLSERIES_SATURATION_HIGH);
	if (ret)
		fprintf(stderr, "Unable to set keyboard color "
			"(left)\n");
	ret = stlseries_setcolor_normal(j2stl->kbd.stlseries,
					STLSERIES_ZONE_CENTER,
					retrieve_color(cabs(fftwp->out[mid]) /
						       (double)(j2stl->audio.size)),
					STLSERIES_SATURATION_HIGH);
	if (ret)
		fprintf(stderr, "Unable to set keyboard color "
			"(center)\n");
	ret = stlseries_setcolor_normal(j2stl->kbd.stlseries,
					STLSERIES_ZONE_RIGHT,
					retrieve_color(cabs(fftwp->out[high]) /
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
	/* FFTW widsom variables */
	char *wisdom_name = NULL;
	int wisdom_name_size;

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

	/* Wisdom name */
	wisdom_name_size = asprintf(&wisdom_name, "%s.wisdom",
				    j2stl->status.progname);
	if (wisdom_name_size == -1) {
		fprintf(stderr, "Unable to allocate memory for wisdow filename:"
				" %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	pthread_cleanup_push(free, wisdom_name);

	/* Wisdom import */
	if (fftw_import_wisdom_from_filename(wisdom_name) == 0)
		fprintf(stderr, "Unable to retrieve wisdom from %s\n",
			wisdom_name);

	/* Plan creation */
	fftwh.p = fftw_plan_dft_1d(j2stl->audio.size, fftwh.in, fftwh.out,
				   FFTW_FORWARD, FFTW_PATIENT);
	pthread_cleanup_push(fftw_destroy_plan, fftwh.p);
	if (fftw_export_wisdom_to_filename(wisdom_name) == 0)
		fprintf(stderr, "Unable to export wisdom to %s\n", wisdom_name);

	/* Process loop */
	while (1) {
		process_loop(j2stl, &fftwh);
	}

	/* Should never get there */

#ifdef _OUTPUT
	fclose(fp);
#endif

	pthread_cleanup_pop(1); /* plan cleanup */
	pthread_cleanup_pop(1); /* wisdom name */
	pthread_cleanup_pop(1); /* fftw_free out */
	pthread_cleanup_pop(1); /* fftw_free in */
	pthread_cleanup_pop(1); /* free raw_data */

	return NULL;
}

