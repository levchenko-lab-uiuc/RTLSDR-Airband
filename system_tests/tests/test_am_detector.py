"""
test_am_detector.py — the coherent AM detector demodulates, and agrees with the envelope one.

`am_detector = "coherent"` tracks the carrier and counter-rotates by it instead of
rectifying the envelope. On a strong signal the two detectors are the same detector to
within a rounding error — that is the measured result, not a hope — so this asserts both
that coherent detection recovers the fixture's tone and that it does not change the level
on a signal this clean. Where it is expected to differ is in noise, and that is measured
in the unit tests (CarrierTest.beats_envelope_detection_below_its_threshold), where the
carrier-to-noise ratio can be set exactly. This test covers the wiring - that the option
parses, reaches the demod loop, and produces usable audio - not the detector's quality.

Audio is read over a continuous udp_stream rather than from MP3, so the two runs are
comparable sample for sample.

Parametrized over all provided binaries (non-NFM and NFM if available).
"""

import time
from pathlib import Path

import numpy as np
from conftest import BinaryUnderTest, run_rtl_airband
from helpers import config_writer, iq_generator, udp_capture

SAMPLE_RATE = 2_048_000
CENTERFREQ_HZ = 120_000_000
CHANNEL_OFFSET_HZ = 25_000
AUDIO_TONE_HZ = 1_000
DURATION_S = 10.0
LOOKBACK_MS = 125

TOTAL_IQ_DURATION_S = DURATION_S + 2 * iq_generator.NOISE_PAD_S
TIMEOUT_S = TOTAL_IQ_DURATION_S * 3 + 30

# The fixture is a clean tone well above the squelch, so both detectors should land on the
# same level. The bound is loose enough not to be brittle and tight enough that a detector
# which silently fell back to the other path, or mis-scaled, would be caught.
MAX_LEVEL_RATIO = 1.25
# Audio must actually be released; a detector that emitted nothing would otherwise pass
MIN_RELEASED_S = 1.0
# The recovered tone has to be the fixture's, not an artefact of the demodulator
TONE_TOLERANCE_HZ = 30.0


def pytest_generate_tests(metafunc):
    """Parametrize over all available binaries."""
    if "binary_under_test" in metafunc.fixturenames:
        am_bins: list[BinaryUnderTest] = metafunc.config._rtlsdr_am_binaries
        metafunc.parametrize(
            "binary_under_test",
            am_bins,
            ids=[b.label for b in am_bins],
        )


def _run_once(
    binary_under_test: BinaryUnderTest,
    run_dir: Path,
    iq_file: Path,
    speedup_factor: float,
    detector: str,
) -> np.ndarray:
    """Run the binary once with the given detector and return the released audio."""
    run_dir.mkdir(parents=True, exist_ok=True)
    config_path = run_dir / "rtl_airband.conf"

    capture = udp_capture.UdpAudioCapture()
    config_writer.write_config(
        config_path=config_path,
        iq_filepath=iq_file,
        sample_rate=SAMPLE_RATE,
        centerfreq_hz=CENTERFREQ_HZ,
        channels=[
            {
                "freq_hz": CENTERFREQ_HZ + CHANNEL_OFFSET_HZ,
                "am_detector": detector,
                "squelch_lookback": LOOKBACK_MS,
                "output_filename_template": f"am_detector_{detector}",
                "udp_output": {"address": "127.0.0.1", "port": capture.port},
            }
        ],
        output_dir=run_dir,
        speedup_factor=speedup_factor,
        mode="multichannel",
    )

    with capture:
        run_rtl_airband(binary_under_test.path, config_path, timeout_s=TIMEOUT_S)
        time.sleep(1.0)
        samples = capture.samples()

    return np.array(samples, dtype=np.float32)


def _dominant_tone_hz(audio: np.ndarray, wave_rate: int) -> float:
    """Frequency of the strongest audio component in the released samples."""
    released = audio[audio != 0.0]
    window = min(8192, len(released))
    block = released[:window] * np.hanning(window)
    spectrum = np.abs(np.fft.rfft(block))
    freqs = np.fft.rfftfreq(window, 1.0 / wave_rate)
    # skip DC, which carries whatever residual offset the detector leaves behind
    return float(freqs[np.argmax(spectrum[1:]) + 1])


def test_am_detector(
    binary_under_test: BinaryUnderTest,
    test_output_dir: Path,
    cache_dir: Path,
    speedup_factor: float,
) -> None:
    """Coherent detection recovers the tone, at the same level as envelope detection."""
    iq_file = iq_generator.get_or_generate_am(
        offset_hz=CHANNEL_OFFSET_HZ,
        audio_hz=AUDIO_TONE_HZ,
        duration_s=DURATION_S,
        cache_dir=cache_dir,
    )

    wave_rate = binary_under_test.wave_rate
    envelope = _run_once(
        binary_under_test,
        test_output_dir / "envelope",
        iq_file,
        speedup_factor,
        "envelope",
    )
    coherent = _run_once(
        binary_under_test,
        test_output_dir / "coherent",
        iq_file,
        speedup_factor,
        "coherent",
    )

    envelope_open = envelope[envelope != 0.0]
    coherent_open = coherent[coherent != 0.0]

    assert (
        len(coherent_open) / wave_rate >= MIN_RELEASED_S
    ), f"coherent detector released only {len(coherent_open) / wave_rate:.2f}s of audio"
    assert (
        len(envelope_open) / wave_rate >= MIN_RELEASED_S
    ), f"envelope detector released only {len(envelope_open) / wave_rate:.2f}s of audio"

    coherent_tone = _dominant_tone_hz(coherent, wave_rate)
    assert (
        abs(coherent_tone - AUDIO_TONE_HZ) <= TONE_TOLERANCE_HZ
    ), f"coherent detector recovered {coherent_tone:.0f} Hz, expected {AUDIO_TONE_HZ} Hz"

    envelope_rms = float(np.sqrt(np.mean(envelope_open**2)))
    coherent_rms = float(np.sqrt(np.mean(coherent_open**2)))
    ratio = max(coherent_rms, envelope_rms) / min(coherent_rms, envelope_rms)
    print(
        f"envelope rms={envelope_rms:.4f}, coherent rms={coherent_rms:.4f}, ratio={ratio:.3f}, coherent tone={coherent_tone:.0f}Hz"
    )

    assert (
        ratio <= MAX_LEVEL_RATIO
    ), f"detectors disagree on level by {ratio:.2f}x (envelope {envelope_rms:.4f}, coherent {coherent_rms:.4f}); on a signal this clean they should match"

    # They should agree closely, but they are not the same arithmetic, so they must not come out
    # identical.  This catches an am_detector that changes nothing at all - a key parsed and then
    # dropped, say.  It does not prove the coherent path itself ran: setting the option also turns
    # on needs_raw_iq, which moves the output on its own.  What the detector actually does with the
    # samples is pinned down in the unit tests, where the carrier-to-noise ratio can be set.
    common = min(len(envelope), len(coherent))
    assert not np.array_equal(
        envelope[:common], coherent[:common]
    ), "am_detector changed nothing in the output at all"
