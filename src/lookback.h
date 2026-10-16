/*
 * lookback.h
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

#ifndef _LOOKBACK_H
#define _LOOKBACK_H

#include <cstddef>  // size_t
#include <cstdint>  // uint8_t

/*
 Theory of operation:

 Squelch cannot decide that a transmission has started until it has already started - the signal
 level has to rise above the threshold and survive the open delay first.  By then the first syllable
 is gone.  AudioLookback gets it back by running the audio path a fixed number of samples behind the
 squelch, so that by the time a sample is emitted, the squelch decisions for the samples that follow
 it are already known and can be applied to it retroactively.

 Two ring buffers of equal length hold the most recent delay() + 1 samples: the signal envelope, and
 the squelch's open/closed decision for each of those samples.  push() adds the newest envelope
 sample and squelch decision; the accessors read out the sample delay() samples older, which is the
 one the caller should be emitting now.  Because the envelope is kept, the caller can demodulate the
 delayed sample rather than re-deriving it.

 agc_envelope() is the envelope delay() - agc_lead samples back, ie. slightly *newer* than the sample
 being emitted.  Keeping the AGC that far ahead of the audio is what the demod loop has always done
 (with a fixed lead of AGC_EXTRA samples) so that the gain is already settled by the time a sample is
 emitted.  The lead stays constant however long the delay gets.

 open_retroactively() is the point of the whole thing.  When the squelch opens - meaning audio is
 actually allowed out, CTCSS included, not merely that the level crossed the threshold - it walks
 backwards through the stored envelope looking for where the signal actually started - the most
 recent window of quiet_run samples whose mean is below a threshold part way between the noise floor
 and the squelch level - and marks every sample from there forward as open.  Those samples have not
 been emitted yet, so the transmission is released from its true start.  Reaching back only as far
 as the signal extends is what keeps a long delay from prepending a long stretch of noise to every
 transmission: the delay is a budget for how far back the squelch may reach, not a fixed pre-roll.

 The test is on the mean of the window rather than on every sample in it because a real AM noise
 floor fluctuates several dB sample to sample.  A window in which every sample is below the
 threshold is uncommon in noise, so requiring one makes the budget behave as exactly the fixed
 pre-roll it is meant not to be.  The mean crosses the threshold where the signal does.

 Walking back to the *start of the signal* rather than to the point of detection matters, because the
 point of detection is precisely what is late.  The threshold is deliberately below the squelch level
 so the backfill covers the part of the rising edge that was too weak to open the squelch.

 NOTE: instances live inside calloc()'d channel_t, so the constructor does not run.  An all-zero
 object is a valid "not configured" state, configure() does the allocation, and every accessor is
 safe (and silent) before then.
 */
class AudioLookback {
   public:
    AudioLookback(void);
    ~AudioLookback(void);

    // Allocate for up to max_delay samples of lookback.  agc_lead is how far ahead of the emitted
    // sample the AGC should track, quiet_run how many consecutive samples below the threshold count
    // as a gap between transmissions.  Both are capped to the delay in use.
    void configure(const size_t& max_delay, const size_t& agc_lead, const size_t& quiet_run);

    // Select the delay in use (<= max_delay) and drop all history.  Also used on its own, via
    // reset(), when the history stops being meaningful - after a scan mode retune the buffered
    // samples belong to the previous frequency and must not be released.
    void set_delay(const size_t& delay);
    void reset(void);

    // Returns true when this sample is the one squelch opened on, ie. the moment to reach back
    bool push(const float& envelope, const bool& squelch_open);
    void open_retroactively(const float& threshold);

    const float& delayed_envelope(void) const;
    const float& agc_envelope(void) const;
    const float& envelope_at_age(const size_t& age) const;
    bool gate(void) const;
    // The squelch decision lead samples *after* the sample being emitted.  Unlike the envelope
    // this does not flicker on modulation, so it is the dependable warning that a transmission
    // is ending: the gate shuts a fixed close delay after the carrier went, and looking that
    // far ahead puts the fade out in front of the collapse.  lead is clamped to the delay.
    bool gate_at_lead(const size_t& lead) const;

    bool is_configured(void) const;
    const size_t& delay(void) const;
    const size_t& agc_lead(void) const;
    const size_t& backfill_count(void) const;

    // Level below which the envelope is considered to be between transmissions: the geometric mean
    // of the noise floor and the squelch level, ie. half way between the two in dB.
    static float backfill_threshold(const float& noise_floor, const float& squelch_level);

   private:
    size_t index_at_age(const size_t& age) const;

    size_t max_delay_;
    size_t delay_;
    size_t agc_lead_cfg_;   // as configured, before clamping to the delay in use
    size_t quiet_run_cfg_;  // as configured, before clamping to the delay in use
    size_t agc_lead_;
    size_t quiet_run_;
    size_t capacity_;
    size_t head_;            // index of the newest sample
    size_t sample_count_;    // samples pushed since the last reset, saturates at capacity_
    size_t backfill_count_;  // samples released retroactively, for logging and tests
    float* envelope_;
    uint8_t* open_;
};

#endif /* _LOOKBACK_H */
