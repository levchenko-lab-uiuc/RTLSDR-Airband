"""
test_mp3_options.py — the MP3 encoder settings reach the encoder.

Bitrate, quality and VBR mode were compiled into airlame_init(). They are now config
options, defaulting to the values that were hardcoded, so an existing config encodes
exactly as it did.

Asserting on the encoded size rather than on a header field: a bitrate that is parsed and
then dropped would still produce a valid MP3 with a plausible header, and only the amount
of data actually written distinguishes one that reached LAME from one that did not.

Every setting under test is encoded in a *single* run, by several channels on the same
frequency differing only in their mp3 options.  Comparing sizes across separate runs does
not work: fast mode replays at a speedup and tolerates a few output overruns, each of
which drops a batch of audio, so two runs of the same config routinely differ by a few
hundred bytes.  Measured under load, seven of eight pairs disagreed.  Channels in one run
share the device's overruns, so whatever is dropped is dropped from all of them equally
and the comparison holds however loaded the machine is.

Parametrized over all provided binaries (non-NFM and NFM if available).
"""

import time
from pathlib import Path

from conftest import BinaryUnderTest, run_rtl_airband
from helpers import config_writer, iq_generator

SAMPLE_RATE = 2_048_000
CENTERFREQ_HZ = 120_000_000
CHANNEL_OFFSET_HZ = 25_000
AUDIO_TONE_HZ = 1_000
DURATION_S = 10.0

TOTAL_IQ_DURATION_S = DURATION_S + 2 * iq_generator.NOISE_PAD_S
TIMEOUT_S = TOTAL_IQ_DURATION_S * 3 + 30

# Constant bitrate at each end of a wide span, so the sizes cannot overlap however the
# encoder distributes bits within a file.
LOW_KBPS = 16
HIGH_KBPS = 128
# 8x the bitrate should be well over 3x the bytes even allowing for headers and padding
MIN_SIZE_RATIO = 3.0


def pytest_generate_tests(metafunc):
    """Parametrize over all available binaries."""
    if "binary_under_test" in metafunc.fixturenames:
        am_bins: list[BinaryUnderTest] = metafunc.config._rtlsdr_am_binaries
        metafunc.parametrize(
            "binary_under_test",
            am_bins,
            ids=[b.label for b in am_bins],
        )


def _encode_variants(
    binary_under_test: BinaryUnderTest,
    run_dir: Path,
    iq_file: Path,
    speedup_factor: float,
    variants: dict[str, dict | None],
) -> dict[str, int]:
    """Encode every variant in one run and return its MP3 bytes, keyed by name.

    All channels sit on the same frequency, so they demodulate identical audio and share
    any dropped batches.  They are told apart by their filename template.
    """
    run_dir.mkdir(parents=True, exist_ok=True)
    mp3_dir = run_dir / "mp3"
    mp3_dir.mkdir(exist_ok=True)
    config_path = run_dir / "rtl_airband.conf"

    config_writer.write_config(
        config_path=config_path,
        iq_filepath=iq_file,
        sample_rate=SAMPLE_RATE,
        centerfreq_hz=CENTERFREQ_HZ,
        channels=[
            {
                "freq_hz": CENTERFREQ_HZ + CHANNEL_OFFSET_HZ,
                "output_filename_template": name,
                **(mp3 or {}),
            }
            for name, mp3 in variants.items()
        ],
        output_dir=run_dir,
        speedup_factor=speedup_factor,
        mode="multichannel",
        mp3_tmp_dir=mp3_dir,
    )

    run_rtl_airband(binary_under_test.path, config_path, timeout_s=TIMEOUT_S)
    time.sleep(0.5)
    return {
        name: sum(f.stat().st_size for f in mp3_dir.rglob(f"{name}*.mp3"))
        for name in variants
    }


def test_mp3_bitrate_changes_the_encoded_size(
    binary_under_test: BinaryUnderTest,
    test_output_dir: Path,
    cache_dir: Path,
    speedup_factor: float,
) -> None:
    """A higher configured bitrate must produce a proportionally larger file."""
    iq_file = iq_generator.get_or_generate_am(
        offset_hz=CHANNEL_OFFSET_HZ,
        audio_hz=AUDIO_TONE_HZ,
        duration_s=DURATION_S,
        cache_dir=cache_dir,
    )

    sizes = _encode_variants(
        binary_under_test,
        test_output_dir,
        iq_file,
        speedup_factor,
        {
            "low": {"mp3_bitrate": LOW_KBPS, "mp3_vbr": False},
            "high": {"mp3_bitrate": HIGH_KBPS, "mp3_vbr": False},
        },
    )
    low, high = sizes["low"], sizes["high"]

    assert low > 0, "no MP3 was produced at the low bitrate"
    assert high > 0, "no MP3 was produced at the high bitrate"
    ratio = high / low
    print(
        f"{LOW_KBPS} kbps -> {low} bytes, {HIGH_KBPS} kbps -> {high} bytes, ratio {ratio:.2f}"
    )

    assert (
        ratio >= MIN_SIZE_RATIO
    ), f"{HIGH_KBPS} kbps produced only {ratio:.2f}x the bytes of {LOW_KBPS} kbps; the setting is not reaching the encoder"


def test_mp3_defaults_are_unchanged(
    binary_under_test: BinaryUnderTest,
    test_output_dir: Path,
    cache_dir: Path,
    speedup_factor: float,
) -> None:
    """Omitting the options must encode exactly as setting the historic values does."""
    iq_file = iq_generator.get_or_generate_am(
        offset_hz=CHANNEL_OFFSET_HZ,
        audio_hz=AUDIO_TONE_HZ,
        duration_s=DURATION_S,
        cache_dir=cache_dir,
    )

    sizes = _encode_variants(
        binary_under_test,
        test_output_dir,
        iq_file,
        speedup_factor,
        {
            "implicit": None,
            "explicit": {"mp3_bitrate": 16, "mp3_quality": 7, "mp3_vbr": True},
        },
    )
    implicit, explicit = sizes["implicit"], sizes["explicit"]

    assert implicit > 0, "no MP3 was produced with default settings"
    print(f"default {implicit} bytes, explicit {explicit} bytes")
    assert (
        implicit == explicit
    ), f"defaults encode differently ({implicit} vs {explicit} bytes) from the values they are supposed to reproduce"
