/*
 * helper_functions.h
 *
 * Copyright (C) 2023 charlie-foxtrot
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _HELPER_FUNCTIONS_H
#define _HELPER_FUNCTIONS_H

#include <ctime>  // struct tm
#include <string>

namespace libconfig {
class Setting;
}

bool dir_exists(const std::string& dir_path);
bool file_exists(const std::string& file_path);
bool make_dir(const std::string& dir_path);
bool make_subdirs(const std::string& basedir, const std::string& subdirs);
std::string make_dated_subdirs(const std::string& basedir, const struct tm* time);
bool should_close_split_file(double duration_sec, double idle_sec, double split_min_file_time, double split_max_file_time, double split_max_idle_time);
bool valid_split_file_times(double split_min_file_time, double split_max_file_time, double split_max_idle_time);
extern const char* const split_file_times_constraint;
bool setting_as_double(const libconfig::Setting& setting, double* value);
bool setting_as_double_or(const libconfig::Setting& parent, const char* key, double fallback, double* value);

// Width of one FFT bin in Hz.  The division is done in floating point: fft_size does not
// divide every sample rate evenly (10 Msps over 2048 bins is 4882.8125 Hz), and truncating
// it drifts by a whole bin for channels far enough from the centre frequency.
double fft_bin_width(int sample_rate, size_t fft_size);

// Index of the FFT bin that carries freq, for a device tuned to centerfreq.
size_t fft_bin_for(int freq, int centerfreq, int sample_rate, size_t fft_size);

// Ramp position for the audio gate, stepped once per sample towards target.  Rising and
// falling use different rates: a transmission whose start squelch_lookback recovered must not
// have that onset softened, while the end has to be ramped away rather than emitted flat and
// then cut, which lands as a click.  target is not always 0 or 1 - ducking to a floor rather
// than closing is what keeps a carrier that flickers from punching holes in the audio.
float next_fade_level(float current, float target, size_t attack_samples, size_t release_samples);

#endif /* _HELPER_FUNCTIONS_H */
