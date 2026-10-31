"""
test_rawfile.py — the rawfile output carries the whole channel baseband.

A `rawfile` output writes the channel's complex I/Q (.cf32 at WAVE_RATE). With
`continuous = true` it must carry every sample, including the periods where the squelch is
shut: that is the record of what was on the air, and it is what makes the file usable to
diagnose what the squelch did.

What this guards against is subtle. The I/Q written is AGC_EXTRA samples old, but the gate
the audio path applies is the lookback-delayed one, so gating the I/Q by the audio's gate
zeroes a window of every transmission's start — 118.75 ms at 16 kHz with squelch_lookback
set — and the onset never reaches the file at all.

Parametrized over all provided binaries (non-NFM and NFM if available).
"""

import time
from pathlib import Path

import numpy as np
from conftest import BinaryUnderTest, run_rtl_airband
from helpers import config_writer, iq_generator

SAMPLE_RATE = 2_048_000
CENTERFREQ_HZ = 120_000_000
CHANNEL_OFFSET_HZ = 25_000
AUDIO_TONE_HZ = 1_000
DURATION_S = 6.0

TOTAL_IQ_DURATION_S = DURATION_S + 2 * iq_generator.NOISE_PAD_S
TIMEOUT_S = TOTAL_IQ_DURATION_S * 3 + 30

# well inside the leading noise pad: the squelch is shut here
PAD_PROBE_S = (0.1, 0.6)
# well inside the tone: the squelch is open here
TONE_PROBE_S = (
    iq_generator.NOISE_PAD_S + 1.0,
    iq_generator.NOISE_PAD_S + DURATION_S - 1.0,
)


def pytest_generate_tests(metafunc):
    """Parametrize over all available binaries."""
    if "binary_under_test" in metafunc.fixturenames:
        am_bins: list[BinaryUnderTest] = metafunc.config._rtlsdr_am_binaries
        metafunc.parametrize(
            "binary_under_test",
            am_bins,
            ids=[b.label for b in am_bins],
        )


def _run(binary_under_test, test_output_dir, cache_dir, speedup_factor, continuous):
    iq_file = iq_generator.get_or_generate_am(
        offset_hz=CHANNEL_OFFSET_HZ,
        audio_hz=AUDIO_TONE_HZ,
        duration_s=DURATION_S,
        cache_dir=cache_dir,
    )
    raw_dir = test_output_dir / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)
    config_path = test_output_dir / "rtl_airband.conf"
    config_writer.write_config(
        config_path=config_path,
        iq_filepath=iq_file,
        sample_rate=SAMPLE_RATE,
        centerfreq_hz=CENTERFREQ_HZ,
        channels=[
            {
                "freq_hz": CENTERFREQ_HZ + CHANNEL_OFFSET_HZ,
                "output_filename_template": "raw",
                "squelch_lookback": 125,
                "rawfile_output": {"directory": str(raw_dir), "continuous": continuous},
            }
        ],
        output_dir=test_output_dir,
        speedup_factor=speedup_factor,
        mode="multichannel",
    )
    run_rtl_airband(binary_under_test.path, config_path, timeout_s=TIMEOUT_S)
    time.sleep(0.5)
    files = sorted(raw_dir.rglob("*.cf32"))
    assert files, "no .cf32 was written"
    data = np.concatenate([np.fromfile(f, dtype=np.complex64) for f in files])
    return data


def _probe(data: np.ndarray, rate: int, window: tuple[float, float]) -> np.ndarray:
    return data[int(window[0] * rate) : int(window[1] * rate)]


def test_rawfile_continuous_carries_the_closed_periods(
    binary_under_test: BinaryUnderTest,
    test_output_dir: Path,
    cache_dir: Path,
    speedup_factor: float,
) -> None:
    """With continuous = true the file is the full timeline, squelch shut or not."""
    rate = binary_under_test.wave_rate
    data = _run(
        binary_under_test, test_output_dir, cache_dir, speedup_factor, continuous=True
    )

    expected = TOTAL_IQ_DURATION_S * rate
    assert (
        abs(len(data) - expected) < 0.5 * rate
    ), f"continuous rawfile holds {len(data)/rate:.2f}s of I/Q, expected about {TOTAL_IQ_DURATION_S:.2f}s"

    pad = _probe(data, rate, PAD_PROBE_S)
    nonzero = np.count_nonzero(pad) / len(pad)
    # noise is never exactly zero, so zeros here are the gate, not the air
    assert nonzero > 0.9, (
        f"only {100*nonzero:.0f}% of the I/Q in the leading noise pad is non-zero: the rawfile is "
        "still being gated by the squelch and is missing what was on the air while it was shut"
    )

    tone = _probe(data, rate, TONE_PROBE_S)
    pad_level = float(np.median(np.abs(pad)))
    tone_level = float(np.median(np.abs(tone)))
    assert tone_level > 3.0 * pad_level, (
        f"tone region |z| {tone_level:.4f} is not clearly above the noise pad's {pad_level:.4f}; "
        "the file does not look like the channel's I/Q"
    )
    print(
        f"{len(data)/rate:.2f}s of I/Q; pad {100*nonzero:.0f}% non-zero at |z|~{pad_level:.4f}, "
        f"tone |z|~{tone_level:.4f}"
    )


def test_rawfile_gated_skips_closed_batches(
    binary_under_test: BinaryUnderTest,
    test_output_dir: Path,
    cache_dir: Path,
    speedup_factor: float,
) -> None:
    """With continuous = false, whole batches with no signal are not written, as before."""
    rate = binary_under_test.wave_rate
    data = _run(
        binary_under_test, test_output_dir, cache_dir, speedup_factor, continuous=False
    )
    total = TOTAL_IQ_DURATION_S * rate
    # the two noise pads are a quarter of the fixture; most of that must be absent
    assert (
        len(data) < 0.9 * total
    ), f"gated rawfile holds {len(data)/rate:.2f}s of {TOTAL_IQ_DURATION_S:.2f}s; closed batches were written"
    assert (
        len(data) > 0.5 * DURATION_S * rate
    ), "gated rawfile is missing most of the tone"
    print(f"gated: {len(data)/rate:.2f}s of {TOTAL_IQ_DURATION_S:.2f}s")
