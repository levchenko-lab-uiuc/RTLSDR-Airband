/*
 * carrier.cpp
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

#include "carrier.h"

#include <cmath>

#include "logging.h"

namespace {

const float TWO_PI = 6.283185307179586f;

// Fewer samples than this cannot separate a carrier from a couple of noise spikes.  At 8 kHz this
// is 2 ms, well inside the lookback budget.
const size_t MIN_ESTIMATE_SAMPLES = 16;

float wrap_phase(float phi) {
    while (phi > (float)M_PI) {
        phi -= TWO_PI;
    }
    while (phi <= -(float)M_PI) {
        phi += TWO_PI;
    }
    return phi;
}

// One-pole coefficient for a corner at hz, given the sample rate
float alpha_for(const float& hz, const float& sample_rate) {
    if (sample_rate <= 0.0f || hz <= 0.0f) {
        return 1.0f;
    }
    const float a = TWO_PI * hz / sample_rate;
    return a >= 1.0f ? 1.0f : a;
}

}  // namespace

CarrierTracker::CarrierTracker(void) {
    // The same state calloc() leaves behind, so an instance inside a calloc()'d channel_t whose
    // constructor never ran behaves identically to one built here
    kp_ = 0.0f;
    ki_ = 0.0f;
    amp_alpha_ = 0.0f;
    lock_alpha_ = 0.0f;
    phase_ = 0.0f;
    dphi_ = 0.0f;
    amplitude_ = 0.0f;
    lock_ = 0.0f;
    tracking_ = false;
    configured_ = false;
}

void CarrierTracker::configure(const float& sample_rate, const float& loop_hz, const float& damping, const float& amplitude_hz) {
    // Standard discrete second-order loop, valid while the natural frequency is well below the
    // sample rate - which it is by design here, since this only has to follow drift.  These are
    // the steady-state Kalman gains for a [phase, frequency] state; loop_hz and damping are a
    // reparameterisation of the process and measurement noise, and are the more familiar pair.
    const float wn_t = TWO_PI * loop_hz / sample_rate;
    kp_ = 2.0f * damping * wn_t;
    ki_ = wn_t * wn_t;

    amp_alpha_ = alpha_for(amplitude_hz, sample_rate);
    // Lock follows the amplitude average; it is a confidence, not a signal, so it may be slower
    lock_alpha_ = amp_alpha_ * 0.5f;

    configured_ = true;
    reset();

    debug_print("Created CarrierTracker, kp: %f, ki: %f, amp_alpha: %f\n", kp_, ki_, amp_alpha_);
}

void CarrierTracker::reset(void) {
    phase_ = 0.0f;
    dphi_ = 0.0f;
    amplitude_ = 0.0f;
    lock_ = 0.0f;
    tracking_ = false;
}

CarrierEstimate CarrierTracker::estimate(const float* iq, const size_t& count) {
    CarrierEstimate result = {0.0f, 0.0f, 0.0f, false};

    if (iq == NULL || count < MIN_ESTIMATE_SAMPLES) {
        return result;
    }

    // Frequency from the autocorrelation at increasing lags.  Each term z[n] * conj(z[n-lag]) has
    // the phase advanced over that lag as its argument and the product of the two magnitudes as
    // its modulus, so the sum is a magnitude-weighted average: strong samples carry it, samples in
    // the noise barely contribute.  Amplitude modulation scales the terms without rotating them,
    // so it drops out.
    //
    // Lag 1 alone is not accurate enough.  Its error is about 1/sqrt(count * snr) radians per
    // sample, which at 6 dB over 2000 samples is some 14 Hz at an 8 kHz rate - and an error of
    // even a few Hz spins the counter-rotated window through whole cycles, so the average that
    // yields phase and amplitude cancels itself out.  A longer lag divides that error by its own
    // length, but its argument only gives the phase advance modulo a turn, so it cannot be used on
    // its own.  Walking the lag up in steps, each one unwrapped using the estimate before it,
    // gives both: the previous estimate is always far better than the half-turn needed to resolve
    // the ambiguity, and the last one is the accurate one.
    double acc_re = 0.0;
    double acc_im = 0.0;
    float dphi = 0.0f;
    const size_t max_lag = count / 4;
    for (size_t lag = 1;; lag *= 4) {
        acc_re = 0.0;
        acc_im = 0.0;
        for (size_t n = lag; n < count; n++) {
            const float re1 = iq[2 * n];
            const float im1 = iq[2 * n + 1];
            const float re0 = iq[2 * (n - lag)];
            const float im0 = iq[2 * (n - lag) + 1];
            // z[n] * conj(z[n-lag])
            acc_re += (double)re1 * (double)re0 + (double)im1 * (double)im0;
            acc_im += (double)im1 * (double)re0 - (double)re1 * (double)im0;
        }

        if (acc_re == 0.0 && acc_im == 0.0) {
            // No signal at all at lag 1 means nothing to estimate; at a longer lag, keep the
            // estimate the shorter ones already produced.
            if (lag == 1) {
                return result;
            }
            break;
        }

        const float arg = (float)atan2(acc_im, acc_re);
        if (lag == 1) {
            dphi = arg;
        } else {
            // arg is lag * dphi wrapped into one turn; the running estimate says which turn
            const float turns = ((float)lag * dphi - arg) / TWO_PI;
            dphi = (arg + TWO_PI * roundf(turns)) / (float)lag;
        }

        if (lag * 4 > max_lag) {
            break;
        }
    }

    // Phase and amplitude by counter-rotating the window and averaging.  What is left is the
    // carrier; the modulation averages away to the extent it is zero-mean over the window.
    double sum_re = 0.0;
    double sum_im = 0.0;
    for (size_t n = 0; n < count; n++) {
        const float phi = -dphi * (float)n;
        const float c = cosf(phi);
        const float s = sinf(phi);
        sum_re += (double)iq[2 * n] * c - (double)iq[2 * n + 1] * s;
        sum_im += (double)iq[2 * n] * s + (double)iq[2 * n + 1] * c;
    }
    sum_re /= (double)count;
    sum_im /= (double)count;

    const float amplitude = (float)sqrt(sum_re * sum_re + sum_im * sum_im);
    if (!(amplitude > 0.0f)) {
        return result;
    }

    result.dphi = dphi;
    // Phase of the sample one past the window, so tracking continues from where this left off
    result.phase = wrap_phase((float)atan2(sum_im, sum_re) + dphi * (float)count);
    result.amplitude = amplitude;
    result.valid = true;
    return result;
}

void CarrierTracker::acquire(const CarrierEstimate& estimate) {
    if (!configured_ || !estimate.valid) {
        return;
    }
    phase_ = estimate.phase;
    dphi_ = estimate.dphi;
    amplitude_ = estimate.amplitude;
    lock_ = 1.0f;
    tracking_ = true;
}

float CarrierTracker::track(const float& re, const float& im) {
    if (!configured_ || !tracking_) {
        return 0.0f;
    }

    // Counter-rotate by the carrier phase.  What lands on the real axis is the carrier and its
    // modulation; what lands on the imaginary axis is the phase error, plus the half of the noise
    // in quadrature with the carrier, which goes no further than this.
    const float c = cosf(-phase_);
    const float s = sinf(-phase_);
    const float inphase = re * c - im * s;
    const float quadrature = re * s + im * c;

    // atan2 rather than the imaginary part alone: both parts scale with the modulation, so their
    // ratio does not, and the loop gain stays put through a deeply modulated syllable.
    const float error = (inphase == 0.0f && quadrature == 0.0f) ? 0.0f : atan2f(quadrature, inphase);

    phase_ = wrap_phase(phase_ + dphi_ + kp_ * error);
    dphi_ += ki_ * error;

    amplitude_ += amp_alpha_ * (inphase - amplitude_);
    lock_ += lock_alpha_ * (cosf(error) - lock_);

    return inphase;
}

bool CarrierTracker::is_configured(void) const {
    return configured_;
}

bool CarrierTracker::is_tracking(void) const {
    return tracking_;
}

const float& CarrierTracker::amplitude(void) const {
    return amplitude_;
}

const float& CarrierTracker::dphi(void) const {
    return dphi_;
}

const float& CarrierTracker::lock(void) const {
    return lock_;
}
