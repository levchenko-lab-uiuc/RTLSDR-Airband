/*
 * carrier.h
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

#ifndef _CARRIER_H
#define _CARRIER_H

#include <cstddef>  // size_t

/*
 Theory of operation:

 AM is demodulated here by tracking the carrier rather than rectifying the envelope.  Where
 `sqrt(re*re + im*im)` throws the phase away, this keeps it: the carrier phase is estimated, the
 sample is counter-rotated by it, and the audio is the component left in phase with the carrier.

 What that buys, in the order it matters here:

 - The carrier amplitude becomes an observable number rather than something inferred.  An envelope
   detector has to guess where the DC sits (a one-pole chasing the signal) and what the gain should
   be (an average that may only track upward, or it chases the modulation).  Both fall out of the
   carrier estimate instead, and both are then correct during the first syllable rather than after
   a time constant has elapsed.
 - Carrier loss is visible the sample it happens, so silencing the end of a transmission needs no
   lookahead into future squelch decisions.
 - Below roughly 6-10 dB carrier-to-noise an envelope detector hits its threshold effect: the
   noise-noise cross terms start to dominate and the output degrades far faster than the input.
   Counter-rotating is linear all the way down, and it discards the noise in quadrature with the
   carrier instead of folding it into the output.

 Two stages, split by timescale, because they are different problems:

 estimate() is feed-forward and one-shot.  Given the complex samples a transmission starts with -
 which the lookback delay line is already holding by the time squelch opens - it solves for the
 residual frequency offset, the phase and the carrier amplitude in a single pass over them.  No
 loop, so there is no acquisition transient to trade against lock time; a tracking loop started
 cold at the same instant would still be pulling in across the first syllable, which is exactly
 the audio `squelch_lookback` exists to recover.

 The frequency comes from the argument of the autocorrelation, sum of z[n] * conj(z[n-lag]).
 Each term's argument is the phase advanced over the lag, and each is weighted by the magnitudes
 of the two samples, so loud samples dominate and samples in the noise contribute little.
 Amplitude modulation does not bias it: it scales the terms without rotating them, as long as the
 modulation does not drive the envelope through zero.  Phase and amplitude then come from
 counter-rotating by that frequency and averaging, which leaves the carrier and averages the
 modulation away to the extent it is zero-mean over the window.

 The lag is walked upwards rather than left at 1, because lag 1 is not accurate enough: its error
 is around 1/sqrt(count * snr) radians per sample, and an error of even a few Hz spins the
 counter-rotated window through whole cycles, so the average that yields phase and amplitude
 cancels itself out.  A longer lag divides that error by its own length but only resolves the
 phase advance modulo a turn, so each round unwraps using the estimate before it.

 track() is the slow half, and it is a second-order loop - which is to say the steady-state Kalman
 filter for a [phase, frequency] state with a linearised phase measurement.  Those are the same
 filter; a loop bandwidth and a damping factor are a reparameterisation of the process and
 measurement noise.  What it has to follow is drift, not acquisition: tuner error is static and
 aircraft Doppler changes over seconds, so this can be narrow, which is what makes it quiet.

 The phase detector is atan2 of the counter-rotated sample rather than its imaginary part alone.
 Both the real and imaginary parts scale with the modulation, so their ratio does not - the
 discriminator gain stays constant through a deeply modulated syllable, where a raw imaginary part
 would make the loop gain swing with the audio.

 NOTE: instances live inside a calloc()'d channel_t, so the constructor does not run.  An all-zero
 object is a valid "not configured" state and every accessor is safe before configure().
 */

// What estimate() solves for over a window of complex samples.
struct CarrierEstimate {
    float dphi;       // carrier rotation per sample, radians, residual after downmixing
    float phase;      // carrier phase for the sample *following* the window, radians
    float amplitude;  // carrier amplitude, ie. the level with the modulation averaged out
    bool valid;       // false when the window was too short or held no signal
};

class CarrierTracker {
   public:
    CarrierTracker(void);

    // sample_rate is the audio rate the samples arrive at.  loop_hz is the loop's natural
    // frequency: it only has to follow oscillator drift and Doppler, so it wants to be narrow,
    // a few Hz.  damping of 0.707 is the usual critically-damped-ish choice.  amplitude_hz is
    // the corner of the carrier amplitude average, below the lowest audio frequency so that
    // modulation averages out of it rather than being tracked.
    void configure(const float& sample_rate, const float& loop_hz, const float& damping, const float& amplitude_hz);

    // Feed-forward estimate over interleaved complex samples, count of them (not of floats).
    // Static because it depends on nothing but the window - the lookback can be measured before
    // any tracker exists, and the result handed to acquire().
    static CarrierEstimate estimate(const float* iq, const size_t& count);

    // Arm tracking from an estimate.  The next sample passed to track() is taken to be the one
    // the estimate's phase refers to, so a window measured over the lookback hands over seamlessly
    // to tracking the samples that follow it.
    //
    // rewind_samples winds the phase back by that many samples first.  The caller needs it when
    // the window it measured *starts* at the sample about to be tracked rather than ending at it,
    // which is the case when the estimate is made over a transmission's own opening samples and
    // then those same samples are demodulated.
    void acquire(const CarrierEstimate& estimate, const size_t& rewind_samples = 0);
    void reset(void);

    // One sample.  Returns the component in phase with the carrier, ie. amplitude * (1 + m) for
    // modulation m, and advances the loop.  Audio is (this - amplitude()) / amplitude(); the
    // caller does that division because the AGC and the gate belong to it, not here.
    float track(const float& re, const float& im);

    bool is_configured(void) const;
    bool is_tracking(void) const;
    const float& amplitude(void) const;
    const float& dphi(void) const;
    // 0 when the counter-rotated sample is as likely to be anywhere as in phase with the carrier,
    // 1 when it sits exactly in phase.  A smoothed cosine of the phase error, so it is a lock
    // indicator and a cheap carrier-presence test at once.
    const float& lock(void) const;

   private:
    float kp_;         // loop proportional gain
    float ki_;         // loop integral gain
    float amp_alpha_;  // carrier amplitude average coefficient
    float lock_alpha_;

    float phase_;
    float dphi_;
    float amplitude_;
    float lock_;
    bool tracking_;
    bool configured_;
};

#endif /* _CARRIER_H */
