/*
 * test_lookback.cpp
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

#include <cstdlib>  // calloc, free

#include "test_base_class.h"

#include "lookback.h"

using namespace std;

class LookbackTest : public TestBaseClass {
   protected:
    void SetUp(void) {
        TestBaseClass::SetUp();
        quiet = 0.05;
        loud = 0.75;
        threshold = 0.25;
    }

    void TearDown(void) { TestBaseClass::TearDown(); }

    // push count samples, all with the squelch reported as closed
    void push_closed(AudioLookback& lookback, size_t count, float envelope) {
        for (size_t i = 0; i < count; i++) {
            lookback.push(envelope, false);
        }
    }

    // push count samples alternating between two levels, squelch closed.  This is what a real
    // noise floor looks like to the backfill: fluctuating either side of the threshold rather
    // than sitting steadily below it.
    void push_noisy(AudioLookback& lookback, size_t count, float low, float high) {
        for (size_t i = 0; i < count; i++) {
            lookback.push(i % 2 == 0 ? low : high, false);
        }
    }

    // push count samples and return how many of them came back out of the delay line gated open
    size_t count_open_out(AudioLookback& lookback, size_t count, float envelope, bool squelch_open) {
        size_t opened = 0;
        for (size_t i = 0; i < count; i++) {
            lookback.push(envelope, squelch_open);
            if (lookback.gate()) {
                opened++;
            }
        }
        return opened;
    }

    // the count of samples the delay line has been told to open, backfilled or pushed
    size_t total_open_samples(AudioLookback& lookback, size_t pushed_open) { return lookback.backfill_count() + pushed_open; }

    float quiet;
    float loud;
    float threshold;
};

TEST_F(LookbackTest, unconfigured_object_is_silent) {
    // instances live inside a calloc()'d channel_t, where the constructor never runs, so the
    // all-zero state has to be safe on its own
    AudioLookback* lookback = (AudioLookback*)calloc(1, sizeof(AudioLookback));
    ASSERT_NE(lookback, (AudioLookback*)NULL);

    EXPECT_FALSE(lookback->is_configured());

    // must not crash, and must not pass audio
    lookback->push(loud, true);
    lookback->open_retroactively(threshold);
    EXPECT_FALSE(lookback->gate());
    EXPECT_EQ(lookback->delayed_envelope(), 0.0);
    EXPECT_EQ(lookback->agc_envelope(), 0.0);

    free(lookback);
}

TEST_F(LookbackTest, configure_sets_delay) {
    AudioLookback lookback;
    lookback.configure(100, 10, 5);

    EXPECT_TRUE(lookback.is_configured());
    EXPECT_EQ(lookback.delay(), 100u);

    lookback.set_delay(40);
    EXPECT_EQ(lookback.delay(), 40u);

    // delay is capped at the allocated maximum
    lookback.set_delay(400);
    EXPECT_EQ(lookback.delay(), 100u);
}

TEST_F(LookbackTest, gate_is_closed_until_delay_line_fills) {
    AudioLookback lookback;
    lookback.configure(50, 10, 5);

    for (size_t i = 0; i < 50; i++) {
        lookback.push(loud, true);
        EXPECT_FALSE(lookback.gate()) << "at sample " << i;
    }

    // the 51st sample pushes the first one out the far end
    lookback.push(loud, true);
    EXPECT_TRUE(lookback.gate());
}

TEST_F(LookbackTest, envelope_comes_back_out_delayed) {
    AudioLookback lookback;
    lookback.configure(10, 4, 5);

    for (size_t i = 0; i < 100; i++) {
        lookback.push((float)i, false);
        if (i >= 10) {
            EXPECT_FLOAT_EQ(lookback.delayed_envelope(), (float)(i - 10));
            // the AGC sample leads the emitted one by agc_lead
            EXPECT_FLOAT_EQ(lookback.agc_envelope(), (float)(i - 10 + 4));
        }
    }
}

TEST_F(LookbackTest, squelch_decisions_come_back_out_delayed) {
    AudioLookback lookback;
    lookback.configure(10, 4, 5);

    push_closed(lookback, 20, quiet);
    EXPECT_FALSE(lookback.gate());

    // squelch opens - the gate stays closed until that decision reaches the end of the delay line
    for (size_t i = 0; i < 10; i++) {
        lookback.push(loud, true);
        EXPECT_FALSE(lookback.gate()) << "at sample " << i;
    }
    lookback.push(loud, true);
    EXPECT_TRUE(lookback.gate());
}

TEST_F(LookbackTest, retroactive_open_releases_the_onset) {
    AudioLookback lookback;
    lookback.configure(100, 10, 5);

    // silence, then a signal that the squelch only notices 30 samples in
    push_closed(lookback, 200, quiet);
    push_closed(lookback, 30, loud);
    lookback.open_retroactively(threshold);

    // the onset the squelch missed is now marked open, less the sample at the very foot of the
    // rise that the windowed mean needs before it crosses the threshold
    EXPECT_EQ(lookback.backfill_count(), 29u);

    // draining the delay line must emit every one of them, plus the rest of the transmission,
    // ie. no audio is lost between the retroactive open and the far end of the delay line
    size_t opened = count_open_out(lookback, 200, loud, true);
    opened += count_open_out(lookback, 200, quiet, false);
    EXPECT_EQ(opened, total_open_samples(lookback, 200));
}

TEST_F(LookbackTest, retroactive_open_stops_at_the_start_of_the_signal) {
    AudioLookback lookback;
    lookback.configure(500, 10, 20);

    // only 40 samples of signal, well inside a 500 sample window - the rest must stay closed.
    // The mean of the 20 sample window falls below the threshold once about 6 of its samples are
    // still signal, so the backfill stops a few samples into the rise rather than at its foot
    push_closed(lookback, 1000, quiet);
    push_closed(lookback, 40, loud);
    lookback.open_retroactively(threshold);

    EXPECT_EQ(lookback.backfill_count(), 35u);
}

TEST_F(LookbackTest, retroactive_open_is_capped_by_the_delay) {
    AudioLookback lookback;
    lookback.configure(60, 10, 20);

    // more signal than the window can hold - only the window's worth is released
    push_closed(lookback, 200, quiet);
    push_closed(lookback, 500, loud);
    lookback.open_retroactively(threshold);

    EXPECT_EQ(lookback.backfill_count(), 61u);
}

TEST_F(LookbackTest, brief_dropout_does_not_end_the_backfill) {
    AudioLookback lookback;
    lookback.configure(500, 10, 20);

    // a 10 sample dip is shorter than the 20 sample quiet run, so the backfill reaches past it
    push_closed(lookback, 1000, quiet);
    push_closed(lookback, 40, loud);
    push_closed(lookback, 10, quiet);
    push_closed(lookback, 40, loud);
    lookback.open_retroactively(threshold);

    EXPECT_EQ(lookback.backfill_count(), 85u);
}

TEST_F(LookbackTest, backfill_does_not_reach_a_previous_transmission) {
    AudioLookback lookback;
    lookback.configure(500, 10, 20);

    // an earlier transmission, a real gap, then the one being opened
    push_closed(lookback, 100, loud);
    push_closed(lookback, 50, quiet);
    push_closed(lookback, 30, loud);
    lookback.open_retroactively(threshold);

    EXPECT_EQ(lookback.backfill_count(), 25u);
}

TEST_F(LookbackTest, backfill_stops_in_a_fluctuating_noise_floor) {
    AudioLookback lookback;
    lookback.configure(500, 10, 20);

    // A real noise floor fluctuates either side of the threshold rather than sitting steadily
    // below it, so no window of 20 samples is ever entirely below.  Here the mean noise level is
    // 0.175, comfortably under the 0.25 threshold, but every other sample is over it.  The
    // backfill has to stop at the signal anyway - testing every sample instead of the mean would
    // find no stopping point at all and release the whole 501 sample delay line, prepending a
    // burst of noise to the transmission.
    push_noisy(lookback, 1000, quiet, 0.30);
    push_closed(lookback, 40, loud);
    lookback.open_retroactively(threshold);

    EXPECT_LT(lookback.backfill_count(), 60u);
    EXPECT_GT(lookback.backfill_count(), 30u);
}

TEST_F(LookbackTest, push_reports_the_sample_squelch_opened_on) {
    AudioLookback lookback;
    lookback.configure(50, 10, 5);

    EXPECT_FALSE(lookback.push(quiet, false));
    EXPECT_TRUE(lookback.push(loud, true)) << "the first open sample is the one to reach back from";
    EXPECT_FALSE(lookback.push(loud, true)) << "still the same transmission";
    EXPECT_FALSE(lookback.push(quiet, false));
    EXPECT_TRUE(lookback.push(loud, true)) << "a new transmission reaches back again";
}

TEST_F(LookbackTest, signal_squelch_never_opens_on_is_never_released) {
    AudioLookback lookback;
    lookback.configure(50, 10, 5);

    // A loud signal that squelch holds closed - a CTCSS tone that does not match, say - must
    // never reach back, however strong the envelope in the delay line looks
    for (size_t i = 0; i < 200; i++) {
        EXPECT_FALSE(lookback.push(loud, false)) << "at sample " << i;
    }
    EXPECT_EQ(lookback.backfill_count(), 0u);
    EXPECT_FALSE(lookback.gate());
}

TEST_F(LookbackTest, reset_drops_buffered_audio) {
    AudioLookback lookback;
    lookback.configure(50, 10, 5);

    push_closed(lookback, 200, loud);
    lookback.reset();

    // nothing from before the reset may be released
    lookback.open_retroactively(threshold);
    EXPECT_EQ(lookback.backfill_count(), 0u);

    EXPECT_FALSE(lookback.gate());
    EXPECT_EQ(lookback.delayed_envelope(), 0.0);
}

TEST_F(LookbackTest, set_delay_drops_buffered_audio) {
    AudioLookback lookback;
    lookback.configure(50, 10, 5);

    push_closed(lookback, 200, loud);
    lookback.set_delay(20);

    lookback.open_retroactively(threshold);
    EXPECT_EQ(lookback.backfill_count(), 0u);
    EXPECT_FALSE(lookback.gate());
}

TEST_F(LookbackTest, zero_delay_passes_the_current_sample_straight_through) {
    AudioLookback lookback;
    lookback.configure(0, 10, 5);

    EXPECT_EQ(lookback.delay(), 0u);

    lookback.push(loud, true);
    EXPECT_TRUE(lookback.gate());
    EXPECT_FLOAT_EQ(lookback.delayed_envelope(), loud);
    EXPECT_FLOAT_EQ(lookback.agc_envelope(), loud);

    lookback.push(quiet, false);
    EXPECT_FALSE(lookback.gate());
    EXPECT_FLOAT_EQ(lookback.delayed_envelope(), quiet);
}

TEST_F(LookbackTest, backfill_threshold_sits_between_noise_and_squelch) {
    // half way between the two in dB is their geometric mean
    EXPECT_FLOAT_EQ(AudioLookback::backfill_threshold(1.0, 100.0), 10.0);
    EXPECT_FLOAT_EQ(AudioLookback::backfill_threshold(0.05, 0.2), 0.1);

    // degenerate inputs fall back to the noise floor
    EXPECT_FLOAT_EQ(AudioLookback::backfill_threshold(0.5, 0.5), 0.5);
    EXPECT_FLOAT_EQ(AudioLookback::backfill_threshold(0.5, 0.1), 0.5);
    EXPECT_FLOAT_EQ(AudioLookback::backfill_threshold(0.0, 1.0), 0.0);
}

