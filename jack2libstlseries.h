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

/*
 * Editable fields
 */

/* Sample size, in milliseconds */
#define SAMPLE_DURATION	50

/* Frequency limits */
#define FREQ_LIMIT_BASS_LOW		1
#define FREQ_LIMIT_BASS_HIGH		75
#define FREQ_LIMIT_MEDIUM_LOW		125
#define FREQ_LIMIT_MEDIUM_HIGH		1900
#define FREQ_LIMIT_TREBLE_LOW		2000
#define FREQ_LIMIT_TREBLE_HIGH		24000

/* Colors (upper limits)*/
#define AMP_OFF		0.008
#define AMP_BLUE	0.01
#define AMP_SKY		0.025
#define AMP_GREEN	0.040
#define AMP_YELLOW	0.055
#define AMP_ORANGE	0.070
#define AMP_RED		0.085
#define AMP_PURPLE	0.1


/*
 * Do not edit below this line
 */

#include <jack/jack.h>
#include <stlseries.h>

/*
 * Custom structures
 */

/* jack */
typedef struct {
	jack_port_t *input_port;
	jack_client_t *client;
} jack_data;

/* audio data */
typedef struct {
	unsigned int sample_rate;
	jack_default_audio_sample_t *data;
	size_t size;
	size_t position;
} audio_data;

/* program status and options */
typedef struct {
	pthread_t fftw_thread;
	const char *progname;
	const char *wisdomfile;
	int verbose;
	int timeout;
	int sampling;
} status_data;

/* memory sync */
typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
} memory_sync;

/* Keyboard related data */
typedef struct {
	STLSERIES stlseries;
} keyboard_data;

/* global structure */
struct J2STL_s {
	jack_data jack;
	audio_data audio;
	status_data status;
	memory_sync memsync;
	keyboard_data kbd;
};
typedef struct J2STL_s J2STL;


void *fftw_thread(void *arg);
