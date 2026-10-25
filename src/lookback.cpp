/*
 * lookback.cpp
 *
 * Copyright (C) 2026 charlie-foxtrot
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

#include "lookback.h"

#include <algorithm>  // min
#include <cmath>      // sqrtf
#include <cstdlib>    // calloc, free
#include <cstring>    // memset

#include "logging.h"

static const float zero_sample = 0.0f;

AudioLookback::AudioLookback(void) {
    // The same state calloc() leaves behind, so that an instance inside a calloc()'d channel_t
    // whose constructor never ran behaves identically to one built here
    max_delay_ = 0;
    delay_ = 0;
    agc_lead_cfg_ = 0;
    quiet_run_cfg_ = 0;
    agc_lead_ = 0;
    quiet_run_ = 0;
    capacity_ = 0;
    head_ = 0;
    sample_count_ = 0;
    backfill_count_ = 0;
    envelope_ = NULL;
    open_ = NULL;
}

AudioLookback::~AudioLookback(void) {
    free(envelope_);
    free(open_);
}

void AudioLookback::configure(const size_t& max_delay, const size_t& agc_lead, const size_t& quiet_run) {
    free(envelope_);
    free(open_);

    max_delay_ = max_delay;
    agc_lead_cfg_ = agc_lead;
    quiet_run_cfg_ = quiet_run;
    capacity_ = max_delay_ + 1;
    envelope_ = (float*)calloc(capacity_, sizeof(float));
    open_ = (uint8_t*)calloc(capacity_, sizeof(uint8_t));
    backfill_count_ = 0;

    set_delay(max_delay_);

    debug_print("Created AudioLookback, max_delay: %zu, agc_lead: %zu, quiet_run: %zu\n", max_delay_, agc_lead_, quiet_run_);
}

void AudioLookback::set_delay(const size_t& delay) {
    delay_ = std::min(delay, max_delay_);
    agc_lead_ = std::min(agc_lead_cfg_, delay_);

    // A quiet run longer than the window could never be found, which would make every open reach
    // all the way back.  One sample is the smallest run that can end a backfill.
    quiet_run_ = std::min(quiet_run_cfg_, delay_);
    if (quiet_run_ < 1) {
        quiet_run_ = 1;
    }

    reset();
}

void AudioLookback::reset(void) {
    head_ = 0;
    sample_count_ = 0;
    if (envelope_ != NULL) {
        memset(envelope_, 0, capacity_ * sizeof(float));
        memset(open_, 0, capacity_ * sizeof(uint8_t));
    }
}

bool AudioLookback::is_configured(void) const {
    return envelope_ != NULL;
}

const size_t& AudioLookback::delay(void) const {
    return delay_;
}

const size_t& AudioLookback::agc_lead(void) const {
    return agc_lead_;
}

const size_t& AudioLookback::backfill_count(void) const {
    return backfill_count_;
}

size_t AudioLookback::index_at_age(const size_t& age) const {
    return (head_ + capacity_ - age) % capacity_;
}

bool AudioLookback::push(const float& envelope, const bool& squelch_open) {
    if (!is_configured()) {
        return false;
    }

    const bool was_open = open_[head_] != 0;

    head_ = (head_ + 1) % capacity_;
    envelope_[head_] = envelope;
    open_[head_] = squelch_open ? 1 : 0;

    if (sample_count_ < capacity_) {
        sample_count_++;
    }

    return squelch_open && !was_open;
}

float AudioLookback::backfill_threshold(const float& noise_floor, const float& squelch_level) {
    if (noise_floor <= 0.0f || squelch_level <= noise_floor) {
        return noise_floor;
    }
    return sqrtf(noise_floor * squelch_level);
}

void AudioLookback::open_retroactively(const float& threshold) {
    if (!is_configured() || sample_count_ == 0) {
        return;
    }

    // Only samples actually pushed since the last reset can be released
    const size_t limit = std::min(delay_, sample_count_ - 1);

    // Walk back to the start of the signal: the sample after the most recent window of quiet_run_
    // samples whose *mean* is below the threshold.  Finding no such window means the signal fills
    // the whole delay line, so reach back as far as it allows.
    //
    // The mean is what makes this work against a real noise floor.  An AM envelope fluctuates
    // several dB from sample to sample, so a window in which every single sample happens to fall
    // below the threshold is uncommon even in pure noise - measured on an off-air capture, one
    // occurs about every 270 ms at the threshold this is called with.  Requiring one therefore
    // sends the walk back past the start of the signal and on until the delay line runs out,
    // which prepends a burst of noise to the transmission; because the AGC is still holding the
    // previous transmission's level, AM demodulates that burst into a near full scale DC step,
    // louder than the speech it was supposed to recover.  The mean of the same window sits within
    // a fraction of a dB of the noise floor and crosses the threshold where the signal does.
    size_t oldest = limit;
    double window_sum = 0.0;
    const double window_threshold = (double)threshold * (double)quiet_run_;
    for (size_t age = 1; age <= limit; age++) {
        window_sum += envelope_[index_at_age(age)];
        if (age > quiet_run_) {
            window_sum -= envelope_[index_at_age(age - quiet_run_)];
        }
        if (age >= quiet_run_ && window_sum < window_threshold) {
            oldest = age - quiet_run_;
            break;
        }
    }

    // The squelch reports the open on the sample *before* the state transition takes effect, so
    // age 0 is part of the transmission too and is included here rather than waiting for push()
    for (size_t age = 0; age <= oldest; age++) {
        uint8_t& open = open_[index_at_age(age)];
        if (!open) {
            open = 1;
            backfill_count_++;
        }
    }

    debug_print("Squelch opened retroactively by %zu samples (threshold %f)\n", oldest, threshold);
}

const float& AudioLookback::delayed_envelope(void) const {
    if (!is_configured() || sample_count_ <= delay_) {
        return zero_sample;
    }
    return envelope_[index_at_age(delay_)];
}

const float& AudioLookback::agc_envelope(void) const {
    if (!is_configured() || sample_count_ <= delay_) {
        return zero_sample;
    }
    return envelope_[index_at_age(delay_ - agc_lead_)];
}

const float& AudioLookback::envelope_at_age(const size_t& age) const {
    if (!is_configured() || age >= sample_count_) {
        return zero_sample;
    }
    return envelope_[index_at_age(std::min(age, max_delay_))];
}

bool AudioLookback::gate(void) const {
    // Until the delay line has filled there is nothing to emit
    if (!is_configured() || sample_count_ <= delay_) {
        return false;
    }
    return open_[index_at_age(delay_)] != 0;
}

bool AudioLookback::gate_at_lead(const size_t& lead) const {
    if (!is_configured() || sample_count_ <= delay_) {
        return false;
    }
    return open_[index_at_age(delay_ - std::min(lead, delay_))] != 0;
}
