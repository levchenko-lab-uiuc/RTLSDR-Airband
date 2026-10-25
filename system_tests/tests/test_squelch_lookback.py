"""
test_squelch_lookback.py — squelch_lookback recovers the start of a transmission.

Squelch cannot tell that a transmission has started until the signal level has
risen above the threshold and survived the open delay, so the audio in between
is lost. Running the same fixture twice, once at the default lookback and once
at 250 ms, the second run must release the transmission from earlier on.

Measured on the released audio itself, over a continuous udp_stream, rather than
on MP3 duration. Duration is the wrong proxy twice over: it only resolves 72 ms
frames (and the encoder is fed 125 ms blocks), and it also moves with whatever the
audio path does at the *end* of a transmission, which has nothing to do with the
backfill this is testing.

Parametrized over all provided binaries (non-NFM and NFM if available). Each
parametrized case is identified as test_squelch_lookback[non-nfm] or
test_squelch_lookback[nfm].
"""

import time
from pathlib import Path

from conftest import BinaryUnderTest, run_rtl_airband
from helpers import config_writer, iq_generator, udp_capture

SAMPLE_RATE = 2_048_000
CENTERFREQ_HZ = 120_000_000
CHANNEL_OFFSET_HZ = 25_000
AUDIO_TONE_HZ = 1_000
DURATION_S = 10.0
LOOKBACK_MS = 250

TOTAL_IQ_DURATION_S = DURATION_S + 2 * iq_generator.NOISE_PAD_S  # 12 s
TIMEOUT_S = TOTAL_IQ_DURATION_S * 3 + 30  # 66 s

AGC_EXTRA = 100  # rtl_airband.h: the delay line never holds fewer samples than this

# The fixture's carrier starts abruptly, so the backfill reaches back over squelch's
# open lag and stops at the noise pad before it - it cannot do better than the instant
# the signal starts. What that recovers is therefore the open lag itself, which is a
# fixed 197 samples and so half as long in wall clock at the NFM build's 16 kHz
# WAVE_RATE as at 8 kHz: ~12 ms non-NFM, ~6 ms NFM. The floor clears the smaller of
# the two with margin; the ceiling is the configured budget, which it can never exceed.
MIN_RECOVERED_S = 0.004
MAX_RECOVERED_S = LOOKBACK_MS / 1000.0


def pytest_generate_tests(metafunc):
    """Parametrize test_squelch_lookback over all available binaries."""
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
    lookback_ms: int | None,
) -> float:
    """Run the binary over the fixture once and return when audio was first released.

    The result is in capture time: the delay line holds the audio back by
    max(lookback, AGC_EXTRA) samples, and that offset is removed here so the two
    runs are directly comparable.
    """
    run_dir.mkdir(parents=True, exist_ok=True)
    config_path = run_dir / "rtl_airband.conf"
    filename_template = "squelch_lookback"

    capture = udp_capture.UdpAudioCapture()
    config_writer.write_config(
        config_path=config_path,
        iq_filepath=iq_file,
        sample_rate=SAMPLE_RATE,
        centerfreq_hz=CENTERFREQ_HZ,
        channels=[
            {
                "freq_hz": CENTERFREQ_HZ + CHANNEL_OFFSET_HZ,
                "squelch_lookback": lookback_ms,
                "output_filename_template": filename_template,
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

    # WAVE_RATE is 8000 in a non-NFM build and 16000 in an NFM one, so both the
    # delay line's length in samples and the conversion back to seconds depend on
    # which binary this is.
    wave_rate = binary_under_test.wave_rate
    delay = max(int(wave_rate * (lookback_ms or 0) / 1000), AGC_EXTRA)
    first = udp_capture.first_released_sample(samples, wave_rate)
    return (first - delay) / wave_rate


def test_squelch_lookback(
    binary_under_test: BinaryUnderTest,
    test_output_dir: Path,
    cache_dir: Path,
    speedup_factor: float,
) -> None:
    """squelch_lookback must release audio from before squelch opened."""
    iq_file = iq_generator.get_or_generate_am(
        offset_hz=CHANNEL_OFFSET_HZ,
        audio_hz=AUDIO_TONE_HZ,
        duration_s=DURATION_S,
        cache_dir=cache_dir,
    )

    baseline_s = _run_once(
        binary_under_test,
        test_output_dir / "baseline",
        iq_file,
        speedup_factor,
        lookback_ms=None,
    )
    lookback_s = _run_once(
        binary_under_test,
        test_output_dir / "lookback",
        iq_file,
        speedup_factor,
        lookback_ms=LOOKBACK_MS,
    )

    recovered_s = baseline_s - lookback_s
    print(
        f"baseline opened at {baseline_s:.4f}s, lookback at {lookback_s:.4f}s, "
        f"recovered={recovered_s:.4f}s"
    )

    assert recovered_s >= MIN_RECOVERED_S, (
        f"squelch_lookback={LOOKBACK_MS}ms released audio only {recovered_s:.4f}s "
        f"earlier (baseline opened at {baseline_s:.4f}s, lookback at {lookback_s:.4f}s); "
        f"expected at least {MIN_RECOVERED_S:.4f}s"
    )
    assert recovered_s <= MAX_RECOVERED_S, (
        f"squelch_lookback={LOOKBACK_MS}ms released audio {recovered_s:.4f}s earlier, "
        f"more than the {MAX_RECOVERED_S:.4f}s the configured lookback allows"
    )
