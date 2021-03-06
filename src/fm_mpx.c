/*
 * mpxgen - FM multiplex encoder with Stereo and RDS
 * Copyright (C) 2019 Anthony96922
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "rds.h"
#ifdef RDS2
#include "rds2.h"
#endif
#include "fm_mpx.h"
#include "mpx_carriers.h"
#include "resampler.h"
#include "input.h"

#define FIR_HALF_SIZE	30
#define FIR_SIZE	(2*FIR_HALF_SIZE-1)

// coefficients of the low-pass FIR filter
float low_pass_fir[FIR_HALF_SIZE];
float fir_buffer[2][FIR_SIZE];

float *input_buffer;
float *mpx_buffer;

// SRC
SRC_STATE *resampler;
SRC_DATA resampler_data;

int input;

float mpx_vol;

void set_output_volume(unsigned int vol) {
	if (vol > 100) vol = 100;
	mpx_vol = (vol / 100.0);
}

int polar_stereo;

void set_polar_stereo(unsigned int st) {
	if (st == 0 || st == 1) polar_stereo = st;
}

// subcarrier volumes
float volumes[] = {
	0.09, // pilot tone: 9% modulation
	0.09, // RDS: 4.5% modulation
	0.09, 0.09, 0.09 // RDS 2
};

void set_carrier_volume(unsigned int carrier, int new_volume) {
	if (new_volume == -1) return;
	if (carrier <= 4) {
		if (new_volume >= 0 && new_volume <= 15) {
			volumes[carrier] = new_volume / 100.0;
		} else {
			volumes[carrier] = 0.09;
		}
	}
}

void set_output_ppm(float new_ppm) {
	if (new_ppm < -100 && new_ppm > 100) new_ppm = 0;
	resampler_data.src_ratio = (192000 / (double)MPX_SAMPLE_RATE) + (new_ppm / 1e6);
}

int fm_mpx_open(char *filename, int wait_for_audio, float out_ppm) {
	int cutoff_freq;

	mpx_buffer = malloc(DATA_SIZE * sizeof(float));

	set_output_ppm(out_ppm);

	resampler_data.output_frames = DATA_SIZE;
	resampler_data.data_in = mpx_buffer;

	if ((resampler = resampler_init(1)) == NULL) {
		fprintf(stderr, "Could not create MPX resampler.\n");
		goto error;
	}

	create_mpx_carriers();

	if (filename != NULL) {
		if (!open_input(filename, wait_for_audio)) goto error;
	} else {
		// Pilot tone is off by default when in standalone mode
		volumes[0] = 0;
		return 0;
	}

	input = 1;
	cutoff_freq = 24000;

	input_buffer = malloc(OUT_NUM_FRAMES * sizeof(float));

	// Here we divide this coefficient by two because it will be counted twice
	// when applying the filter
	low_pass_fir[FIR_HALF_SIZE-1] = 2 * cutoff_freq / MPX_SAMPLE_RATE / 2;

	// Only store half of the filter since it is symmetric
	for(int i=1; i<FIR_HALF_SIZE; i++) {
		low_pass_fir[FIR_HALF_SIZE-1-i] =
			sin(2 * M_PI * cutoff_freq * i / MPX_SAMPLE_RATE) / (M_PI * i) // sinc
			* (.54 - .46 * cos(2 * M_PI * (i+FIR_HALF_SIZE) / (2*FIR_HALF_SIZE))); // Hamming window
	}

	fprintf(stderr, "Created low-pass FIR filter for audio channels, with cutoff at %d Hz\n", cutoff_freq);

	return 0;

error:
	fm_mpx_close();
	return -1;
}

int fm_mpx_get_samples(float *out) {
	int j = 0;
	int audio_len;
	static int fir_index;

	int ifbi, dfbi;
	float out_left, out_right;
	float out_mono, out_stereo;

	if (!input) {
		audio_len = IN_NUM_FRAMES;

		for (int i = 0; i < audio_len; i++) {
			// Pilot tone for calibration
			mpx_buffer[i] = get_carrier(CARRIER_19K) * volumes[0];

			mpx_buffer[i] += get_carrier(CARRIER_57K) * get_rds_sample() * volumes[1];

#ifdef RDS2
			mpx_buffer[i] += get_carrier(CARRIER_67K) * get_rds2_sample(1) * volumes[2];
			mpx_buffer[i] += get_carrier(CARRIER_71K) * get_rds2_sample(2) * volumes[3];
			mpx_buffer[i] += get_carrier(CARRIER_76K) * get_rds2_sample(3) * volumes[4];
#endif

			update_carrier_phase();

			mpx_buffer[i] *= mpx_vol;
		}
	} else {
		if ((audio_len = read_input(input_buffer)) < 0) return -1;

		for (int i = 0; i < audio_len; i++) {
			// First store the current sample(s) into the FIR filter's ring buffer
			fir_buffer[0][fir_index] = input_buffer[j+0];
			fir_buffer[1][fir_index] = input_buffer[j+1];
			j += 2;
			fir_index++;
			if(fir_index == FIR_SIZE) fir_index = 0;

			// L/R signals
			out_left  = 0;
			out_right = 0;

			// Now apply the FIR low-pass filter

			/* As the FIR filter is symmetric, we do not multiply all
			   the coefficients independently, but two-by-two, thus reducing
			   the total number of multiplications by a factor of two
			 */
			ifbi = fir_index;  // ifbi = increasing FIR Buffer Index
			dfbi = fir_index;  // dfbi = decreasing FIR Buffer Index
			for(int fi=0; fi<FIR_HALF_SIZE; fi++) {  // fi = Filter Index
				dfbi--;
				if(dfbi < 0) dfbi = FIR_SIZE-1;
				out_left  += low_pass_fir[fi] * (fir_buffer[0][ifbi] + fir_buffer[0][dfbi]);
				out_right += low_pass_fir[fi] * (fir_buffer[1][ifbi] + fir_buffer[1][dfbi]);
				ifbi++;
				if(ifbi == FIR_SIZE) ifbi = 0;
			}
			// End of FIR filter

			// 6dB input gain
			out_left  *= 2;
			out_right *= 2;

			// Create sum and difference signals
			out_mono   = out_left + out_right;
			out_stereo = out_left - out_right;

			// audio signals need to be limited to 45% to remain within modulation limits
			mpx_buffer[i] = out_mono * 0.45;

			if (polar_stereo) {
				// Polar stereo encoding system used in Eastern Europe
				mpx_buffer[i] +=
					get_carrier(CARRIER_31K) * ((out_stereo * 0.45) + volumes[0]);
			} else {
				mpx_buffer[i] +=
					get_carrier(CARRIER_19K) * volumes[0] +
					get_carrier(CARRIER_38K) * out_stereo * 0.45;
			}

			mpx_buffer[i] += get_carrier(CARRIER_57K) * get_rds_sample() * volumes[1];

#ifdef RDS2
			mpx_buffer[i] += get_carrier(CARRIER_67K) * get_rds2_sample(1) * volumes[2];
			mpx_buffer[i] += get_carrier(CARRIER_71K) * get_rds2_sample(2) * volumes[3];
			mpx_buffer[i] += get_carrier(CARRIER_76K) * get_rds2_sample(3) * volumes[4];
#endif

			update_carrier_phase();

			mpx_buffer[i] *= mpx_vol;
		}
	}

	resampler_data.input_frames = audio_len;
	resampler_data.data_out = out;
	if ((audio_len = resample(resampler, resampler_data)) < 0) return -1;

	return audio_len;
}

void fm_mpx_close() {
	close_input();
	clear_mpx_carriers();
	free(mpx_buffer);
	resampler_exit(resampler);
}
