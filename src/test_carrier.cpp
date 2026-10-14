/*
 * test_carrier.cpp
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

#include <cmath>
#include <cstdint>
#include <cstdlib>  // calloc, free
#include <vector>

#include "test_base_class.h"

#include "carrier.h"

using namespace std;

namespace {

const float SAMPLE_RATE = 8000.0f;
const float TWO_PI = 6.283185307179586f;

// An AM carrier: amplitude * (1 + depth * sin(audio)) rotating at offset_hz, sampled as
// interleaved re/im.  phase0 is the carrier phase of the first sample.
vector<float> make_am(size_t count, float amplitude, float offset_hz, float phase0, float depth = 0.0f, float audio_hz = 0.0f) {
    vector<float> iq(2 * count);
    const float dphi = TWO_PI * offset_hz / SAMPLE_RATE;
    const float daudio = TWO_PI * audio_hz / SAMPLE_RATE;
    for (size_t n = 0; n < count; n++) {
        const float env = amplitude * (1.0f + depth * sinf(daudio * (float)n));
        const float phi = phase0 + dphi * (float)n;
        iq[2 * n] = env * cosf(phi);
        iq[2 * n + 1] = env * sinf(phi);
    }
    return iq;
}

// A deterministic generator, rather than rand_r: these tests assert on noise statistics, so the
// same sequence has to come out on every platform the CI builds for.
class Lcg {
   public:
    explicit Lcg(uint32_t seed) : state_(seed) {}

    float uniform(void) {
        state_ = state_ * 1664525u + 1013904223u;
        return (float)(state_ >> 8) / 16777216.0f;
    }

    float gaussian(void) {
        const float u1 = uniform() + 1e-7f;
        const float u2 = uniform();
        return sqrtf(-2.0f * logf(u1)) * cosf(TWO_PI * u2);
    }

   private:
    uint32_t state_;
};

// Additive complex Gaussian noise at the given carrier-to-noise ratio.  Carrier power is
// amplitude^2 for a complex exponential; noise power is 2 * sigma^2 across the two components.
void add_noise(vector<float>& iq, float amplitude, float cnr_db, uint32_t seed) {
    const float noise_power = amplitude * amplitude / powf(10.0f, cnr_db / 10.0f);
    const float sigma = sqrtf(noise_power / 2.0f);
    Lcg rng(seed);
    for (size_t i = 0; i < iq.size(); i++) {
        iq[i] += sigma * rng.gaussian();
    }
}

// Wrap to (-pi, pi] so phase comparisons do not fail across the branch cut
float wrap(float x) {
    while (x > (float)M_PI) {
        x -= TWO_PI;
    }
    while (x <= -(float)M_PI) {
        x += TWO_PI;
    }
    return x;
}

float hz_of(float dphi) {
    return dphi * SAMPLE_RATE / TWO_PI;
}

}  // namespace

class CarrierTest : public TestBaseClass {
   protected:
    void SetUp(void) {
        TestBaseClass::SetUp();
        tracker.configure(SAMPLE_RATE, 5.0f, 0.707f, 20.0f);
    }

    void TearDown(void) { TestBaseClass::TearDown(); }

    CarrierTracker tracker;
};

TEST_F(CarrierTest, unconfigured_object_is_safe) {
    // instances live inside a calloc()'d channel_t, where the constructor never runs
    CarrierTracker* t = (CarrierTracker*)calloc(1, sizeof(CarrierTracker));
    ASSERT_NE(t, (CarrierTracker*)NULL);

    EXPECT_FALSE(t->is_configured());
    EXPECT_FALSE(t->is_tracking());
    EXPECT_EQ(t->track(1.0, 1.0), 0.0);
    EXPECT_EQ(t->amplitude(), 0.0);
    EXPECT_EQ(t->lock(), 0.0);

    free(t);
}

TEST_F(CarrierTest, estimate_recovers_frequency_phase_and_amplitude) {
    const vector<float> iq = make_am(400, 0.5f, 120.0f, 0.8f);

    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 400);

    ASSERT_TRUE(est.valid);
    EXPECT_NEAR(hz_of(est.dphi), 120.0, 0.5);
    EXPECT_NEAR(est.amplitude, 0.5, 0.01);

    // phase is for the sample *after* the window, so that tracking carries straight on
    const float expected = 0.8f + TWO_PI * 120.0f / SAMPLE_RATE * 400.0f;
    EXPECT_NEAR(wrap(est.phase - expected), 0.0, 0.05);
}

TEST_F(CarrierTest, estimate_handles_a_zero_offset_carrier) {
    const vector<float> iq = make_am(400, 0.25f, 0.0f, -1.2f);

    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 400);

    ASSERT_TRUE(est.valid);
    EXPECT_NEAR(hz_of(est.dphi), 0.0, 0.5);
    EXPECT_NEAR(est.amplitude, 0.25, 0.01);
}

TEST_F(CarrierTest, estimate_handles_a_negative_offset) {
    const vector<float> iq = make_am(400, 0.5f, -95.0f, 0.0f);

    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 400);

    ASSERT_TRUE(est.valid);
    EXPECT_NEAR(hz_of(est.dphi), -95.0, 0.5);
}

TEST_F(CarrierTest, modulation_does_not_bias_the_estimate) {
    // 70% modulation at 1 kHz scales each autocorrelation term without rotating it, and averages
    // out of the amplitude over a whole number of audio cycles
    const vector<float> iq = make_am(800, 0.4f, 60.0f, 0.3f, 0.7f, 1000.0f);

    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 800);

    ASSERT_TRUE(est.valid);
    EXPECT_NEAR(hz_of(est.dphi), 60.0, 0.5) << "modulation must not pull the frequency";
    EXPECT_NEAR(est.amplitude, 0.4, 0.02) << "amplitude is the carrier, not the peak envelope";
}

TEST_F(CarrierTest, estimate_rejects_a_window_that_is_too_short) {
    const vector<float> iq = make_am(4, 0.5f, 100.0f, 0.0f);

    EXPECT_FALSE(CarrierTracker::estimate(iq.data(), 4).valid);
    EXPECT_FALSE(CarrierTracker::estimate(iq.data(), 0).valid);
    EXPECT_FALSE(CarrierTracker::estimate(NULL, 400).valid);
}

TEST_F(CarrierTest, estimate_rejects_silence) {
    const vector<float> iq(800, 0.0f);

    EXPECT_FALSE(CarrierTracker::estimate(iq.data(), 400).valid);
}

TEST_F(CarrierTest, acquire_hands_over_seamlessly_to_tracking) {
    // estimate over the first half, track the second: the counter-rotated samples must land in
    // phase with the carrier straight away, with no acquisition transient
    const vector<float> iq = make_am(800, 0.5f, 120.0f, 0.4f);

    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 400);
    ASSERT_TRUE(est.valid);
    tracker.acquire(est);
    EXPECT_TRUE(tracker.is_tracking());

    // the very first tracked sample is already in phase - this is the point of the feed-forward
    // estimate, and what a loop starting cold could not do
    const float first = tracker.track(iq[2 * 400], iq[2 * 400 + 1]);
    EXPECT_NEAR(first, 0.5, 0.02);

    for (size_t n = 401; n < 800; n++) {
        EXPECT_NEAR(tracker.track(iq[2 * n], iq[2 * n + 1]), 0.5, 0.02) << "at sample " << n;
    }
    EXPECT_NEAR(tracker.amplitude(), 0.5, 0.02);
    EXPECT_GT(tracker.lock(), 0.95);
}

TEST_F(CarrierTest, tracking_recovers_the_modulation) {
    const size_t count = 2400;
    const float depth = 0.6f;
    const vector<float> iq = make_am(count, 0.5f, 60.0f, 0.1f, depth, 1000.0f);

    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 400);
    ASSERT_TRUE(est.valid);
    tracker.acquire(est);

    // let the amplitude average settle, then compare recovered modulation against the truth
    for (size_t n = 400; n < 1600; n++) {
        tracker.track(iq[2 * n], iq[2 * n + 1]);
    }

    float worst = 0.0f;
    for (size_t n = 1600; n < count; n++) {
        const float inphase = tracker.track(iq[2 * n], iq[2 * n + 1]);
        const float recovered = (inphase - tracker.amplitude()) / tracker.amplitude();
        const float truth = depth * sinf(TWO_PI * 1000.0f / SAMPLE_RATE * (float)n);
        worst = max(worst, fabsf(recovered - truth));
    }
    EXPECT_LT(worst, 0.05) << "coherent output should follow the modulation index";
}

TEST_F(CarrierTest, tracking_follows_a_slow_frequency_drift) {
    // Doppler changes over seconds, so a drift of a few Hz per second is more than realistic
    const size_t count = 8000;
    vector<float> iq(2 * count);
    float phi = 0.0f;
    for (size_t n = 0; n < count; n++) {
        const float offset_hz = 60.0f + 20.0f * (float)n / (float)count;
        iq[2 * n] = 0.5f * cosf(phi);
        iq[2 * n + 1] = 0.5f * sinf(phi);
        phi += TWO_PI * offset_hz / SAMPLE_RATE;
    }

    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 400);
    ASSERT_TRUE(est.valid);
    tracker.acquire(est);
    for (size_t n = 400; n < count; n++) {
        tracker.track(iq[2 * n], iq[2 * n + 1]);
    }

    EXPECT_NEAR(hz_of(tracker.dphi()), 80.0, 2.0) << "the loop should have followed the drift";
    EXPECT_GT(tracker.lock(), 0.95);
}

TEST_F(CarrierTest, carrier_amplitude_is_unbiased_in_noise) {
    // The point of tracking the carrier rather than rectifying the envelope.  |z| is a Rician
    // magnitude: noise can only push it away from zero, so its mean sits *above* the carrier, and
    // the weaker the signal the worse the bias.  The in-phase component after counter-rotating is
    // the carrier plus zero-mean noise, so its mean is the carrier.
    const float amplitude = 0.5f;
    const size_t count = 16000;
    vector<float> iq = make_am(count, amplitude, 60.0f, 0.2f);
    add_noise(iq, amplitude, 6.0f, 20260413u);

    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 400);
    ASSERT_TRUE(est.valid);
    tracker.acquire(est);

    double envelope_sum = 0.0;
    for (size_t n = 400; n < count; n++) {
        tracker.track(iq[2 * n], iq[2 * n + 1]);
        envelope_sum += sqrt((double)iq[2 * n] * iq[2 * n] + (double)iq[2 * n + 1] * iq[2 * n + 1]);
    }
    const float envelope_mean = (float)(envelope_sum / (double)(count - 400));

    EXPECT_NEAR(tracker.amplitude(), amplitude, 0.03 * amplitude) << "coherent amplitude should be unbiased";
    EXPECT_GT(envelope_mean, amplitude * 1.05) << "the envelope mean should be visibly biased high, or this test proves nothing";
    EXPECT_LT(fabsf(tracker.amplitude() - amplitude), fabsf(envelope_mean - amplitude)) << "coherent must beat the envelope it is replacing";
}

// Output SNR of a detector's samples against the modulation they should carry.  Scale-invariant:
// the best-fit gain is divided out first, because the two detectors do not share an output
// scaling and an RMS error against the truth would simply reward whichever one attenuates more.
double output_snr_db(const vector<double>& out, const vector<double>& truth) {
    double cross = 0.0;
    double power = 0.0;
    for (size_t i = 0; i < out.size(); i++) {
        cross += out[i] * truth[i];
        power += truth[i] * truth[i];
    }
    const double gain = cross / power;
    double residual = 0.0;
    for (size_t i = 0; i < out.size(); i++) {
        const double e = out[i] - gain * truth[i];
        residual += e * e;
    }
    return 10.0 * log10(gain * gain * power / residual);
}

// Runs both detectors over the same noisy AM and returns {coherent, envelope} output SNR in dB.
void compare_detectors(CarrierTracker& tracker, float cnr_db, uint32_t seed, double* coherent_db, double* envelope_db) {
    const float amplitude = 0.5f;
    const float depth = 0.5f;
    const float audio_hz = 1000.0f;
    const size_t count = 48000;
    const size_t settle = 16000;

    vector<float> iq = make_am(count, amplitude, 60.0f, 0.0f, depth, audio_hz);
    add_noise(iq, amplitude, cnr_db, seed);

    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 400);
    ASSERT_TRUE(est.valid);
    tracker.acquire(est);

    double envelope_dc = 0.0;
    for (size_t n = 400; n < settle; n++) {
        tracker.track(iq[2 * n], iq[2 * n + 1]);
        envelope_dc += sqrt((double)iq[2 * n] * iq[2 * n] + (double)iq[2 * n + 1] * iq[2 * n + 1]);
    }
    envelope_dc /= (double)(settle - 400);

    vector<double> coherent, envelope, truth;
    for (size_t n = settle; n < count; n++) {
        const float inphase = tracker.track(iq[2 * n], iq[2 * n + 1]);
        coherent.push_back((inphase - tracker.amplitude()) / tracker.amplitude());
        envelope.push_back((sqrt((double)iq[2 * n] * iq[2 * n] + (double)iq[2 * n + 1] * iq[2 * n + 1]) - envelope_dc) / envelope_dc);
        truth.push_back(depth * sinf(TWO_PI * audio_hz / SAMPLE_RATE * (float)n));
    }

    *coherent_db = output_snr_db(coherent, truth);
    *envelope_db = output_snr_db(envelope, truth);
}

TEST_F(CarrierTest, beats_envelope_detection_below_its_threshold) {
    // The threshold effect, measured.  Below roughly 6 dB carrier-to-noise an envelope detector
    // starts losing the signal to the noise-noise cross terms, while counter-rotating stays
    // linear and throws away the half of the noise in quadrature with the carrier.
    double coherent_db = 0.0;
    double envelope_db = 0.0;
    compare_detectors(tracker, -3.0f, 20260414u, &coherent_db, &envelope_db);

    EXPECT_GT(coherent_db, envelope_db + 2.0) << "coherent " << coherent_db << " dB vs envelope " << envelope_db << " dB";
}

TEST_F(CarrierTest, matches_envelope_detection_on_a_strong_signal) {
    // The other half of the honest answer, and the reason this is not a free win: on a strong
    // signal the two are the same detector to within a rounding error.  Anyone expecting coherent
    // detection to improve a local transmitter should read this test.
    double coherent_db = 0.0;
    double envelope_db = 0.0;
    compare_detectors(tracker, 18.0f, 20260416u, &coherent_db, &envelope_db);

    EXPECT_NEAR(coherent_db, envelope_db, 0.5) << "coherent " << coherent_db << " dB vs envelope " << envelope_db << " dB";
    EXPECT_GT(coherent_db, 10.0) << "both should be clean at this carrier-to-noise";
}

TEST_F(CarrierTest, estimate_still_finds_the_carrier_in_noise) {
    vector<float> iq = make_am(2000, 0.5f, 90.0f, 0.5f, 0.5f, 1000.0f);
    add_noise(iq, 0.5f, 6.0f, 20260415u);

    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 2000);

    ASSERT_TRUE(est.valid);
    EXPECT_NEAR(hz_of(est.dphi), 90.0, 3.0);
    EXPECT_NEAR(est.amplitude, 0.5, 0.05);
}

TEST_F(CarrierTest, lock_falls_away_when_the_carrier_goes) {
    const vector<float> iq = make_am(800, 0.5f, 60.0f, 0.0f);
    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 400);
    tracker.acquire(est);
    for (size_t n = 400; n < 800; n++) {
        tracker.track(iq[2 * n], iq[2 * n + 1]);
    }
    ASSERT_GT(tracker.lock(), 0.95);

    // noise with no carrier in it: the counter-rotated sample stops sitting in phase
    Lcg rng(7u);
    for (size_t n = 0; n < 2000; n++) {
        tracker.track(0.05f * rng.gaussian(), 0.05f * rng.gaussian());
    }
    EXPECT_LT(tracker.lock(), 0.5) << "lock must report that the carrier has gone";
}

TEST_F(CarrierTest, reset_drops_tracking_state) {
    const vector<float> iq = make_am(800, 0.5f, 60.0f, 0.0f);
    tracker.acquire(CarrierTracker::estimate(iq.data(), 400));
    ASSERT_TRUE(tracker.is_tracking());

    tracker.reset();

    EXPECT_FALSE(tracker.is_tracking());
    EXPECT_EQ(tracker.amplitude(), 0.0);
    EXPECT_EQ(tracker.lock(), 0.0);
    EXPECT_EQ(tracker.track(1.0, 1.0), 0.0) << "nothing is emitted until re-acquired";
    EXPECT_TRUE(tracker.is_configured()) << "reset drops state, not configuration";
}

TEST_F(CarrierTest, an_invalid_estimate_does_not_start_tracking) {
    const vector<float> quiet(800, 0.0f);

    tracker.acquire(CarrierTracker::estimate(quiet.data(), 400));

    EXPECT_FALSE(tracker.is_tracking());
    EXPECT_EQ(tracker.track(1.0, 1.0), 0.0);
}

TEST_F(CarrierTest, acquire_can_rewind_to_the_start_of_the_window) {
    // The integration measures a window over a transmission's opening samples and then
    // demodulates those same samples, so tracking has to begin at the window's start.
    const vector<float> iq = make_am(800, 0.5f, 120.0f, 0.4f);

    const CarrierEstimate est = CarrierTracker::estimate(iq.data(), 300);
    ASSERT_TRUE(est.valid);
    tracker.acquire(est, 300);

    // sample 0, the first of the window, must already be in phase
    EXPECT_NEAR(tracker.track(iq[0], iq[1]), 0.5, 0.02);
    for (size_t n = 1; n < 800; n++) {
        EXPECT_NEAR(tracker.track(iq[2 * n], iq[2 * n + 1]), 0.5, 0.02) << "at sample " << n;
    }
}
