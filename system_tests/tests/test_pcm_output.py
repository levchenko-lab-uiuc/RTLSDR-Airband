"""
test_pcm_output.py — the pcmfile output writes gated audio and a usable index.

`type = "pcmfile"` writes 16-bit PCM at WAVE_RATE, keeping only what the squelch passed,
and an .idx beside it giving each transmission's place in two timelines: the continuous
audio, and the file with the silence removed.

What this covers that the unit tests cannot is that the per-sample gate arriving from the
demod loop is the real squelch decision — PcmWriter is tested against a gate handed to it,
while here the gate comes from the binary demodulating an actual fixture.

Parametrized over all provided binaries (non-NFM and NFM if available).
"""

import struct
import time
import wave
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

# The fixture is a single tone bracketed by noise pads, so one transmission is expected and
# it must be a decent fraction of the fixture rather than a fragment.
MIN_AUDIO_S = 1.0


def pytest_generate_tests(metafunc):
    """Parametrize over all available binaries."""
    if "binary_under_test" in metafunc.fixturenames:
        am_bins: list[BinaryUnderTest] = metafunc.config._rtlsdr_am_binaries
        metafunc.parametrize(
            "binary_under_test",
            am_bins,
            ids=[b.label for b in am_bins],
        )


def _index_rows(path: Path) -> list[tuple[int, int, int]]:
    rows = []
    for line in path.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        assert len(parts) == 3, f"index line should have three columns, got {line!r}"
        rows.append(tuple(int(p) for p in parts))
    return rows


def _header_text(path: Path) -> str:
    return "\n".join(
        line for line in path.read_text().splitlines() if line.startswith("#")
    )


def test_pcm_failure_does_not_stop_the_stream(
    binary_under_test: BinaryUnderTest,
    test_output_dir: Path,
    cache_dir: Path,
    speedup_factor: float,
) -> None:
    """A recording that cannot be written must not take the other outputs with it.

    The feed matters more than the archive: if the disk fills or the directory is not
    writable, the channel has to keep producing audio for everything else it feeds.
    """
    iq_file = iq_generator.get_or_generate_am(
        offset_hz=CHANNEL_OFFSET_HZ,
        audio_hz=AUDIO_TONE_HZ,
        duration_s=DURATION_S,
        cache_dir=cache_dir,
    )

    mp3_dir = test_output_dir / "mp3"
    mp3_dir.mkdir(parents=True, exist_ok=True)
    config_path = test_output_dir / "rtl_airband.conf"

    config_writer.write_config(
        config_path=config_path,
        iq_filepath=iq_file,
        sample_rate=SAMPLE_RATE,
        centerfreq_hz=CENTERFREQ_HZ,
        channels=[
            {
                "freq_hz": CENTERFREQ_HZ + CHANNEL_OFFSET_HZ,
                "output_filename_template": "still_running",
                # unwritable: /proc rejects the open outright
                "pcm_output": "/proc/no_such_dir/out_%H.wav",
            }
        ],
        output_dir=test_output_dir,
        speedup_factor=speedup_factor,
        mode="multichannel",
        mp3_tmp_dir=mp3_dir,
    )

    run_rtl_airband(binary_under_test.path, config_path, timeout_s=TIMEOUT_S)
    time.sleep(0.5)

    produced = sum(f.stat().st_size for f in mp3_dir.rglob("*.mp3"))
    assert (
        produced > 0
    ), "the MP3 output stopped when the PCM recording could not be written"
    print(
        f"pcm output failed as intended; the channel still produced {produced} bytes of MP3"
    )


def test_pcm_output(
    binary_under_test: BinaryUnderTest,
    test_output_dir: Path,
    cache_dir: Path,
    speedup_factor: float,
) -> None:
    """Audio is gated, the WAV is valid, and the index matches it."""
    iq_file = iq_generator.get_or_generate_am(
        offset_hz=CHANNEL_OFFSET_HZ,
        audio_hz=AUDIO_TONE_HZ,
        duration_s=DURATION_S,
        cache_dir=cache_dir,
    )

    out_dir = test_output_dir / "pcm"
    out_dir.mkdir(parents=True, exist_ok=True)
    config_path = test_output_dir / "rtl_airband.conf"

    config_writer.write_config(
        config_path=config_path,
        iq_filepath=iq_file,
        sample_rate=SAMPLE_RATE,
        centerfreq_hz=CENTERFREQ_HZ,
        channels=[
            {
                "freq_hz": CENTERFREQ_HZ + CHANNEL_OFFSET_HZ,
                "output_filename_template": "pcm",
                "pcm_output": str(out_dir / "%Y%m%d_%H.wav"),
            }
        ],
        output_dir=test_output_dir,
        speedup_factor=speedup_factor,
        mode="multichannel",
    )

    run_rtl_airband(binary_under_test.path, config_path, timeout_s=TIMEOUT_S)
    time.sleep(0.5)

    wavs = sorted(out_dir.glob("*.wav"))
    assert wavs, "no WAV file was written"
    idxs = sorted(out_dir.glob("*.idx"))
    assert len(idxs) == len(wavs), "every WAV must have an index beside it"

    wave_rate = binary_under_test.wave_rate
    total_frames = 0
    total_rows: list[tuple[int, int, int]] = []

    for wav_path in wavs:
        with wave.open(str(wav_path)) as w:
            assert w.getnchannels() == 1, "expected mono"
            assert w.getsampwidth() == 2, "expected 16-bit"
            assert (
                w.getframerate() == wave_rate
            ), f"expected {wave_rate} Hz, got {w.getframerate()}"
            frames = w.getnframes()
            # the header's length fields are patched on close; a zero here means that failed
            assert (
                frames > 0
            ), f"{wav_path.name} reports no frames, so its header was never patched"
            raw = w.readframes(frames)

        # gated audio should not be silence
        peak = max(abs(v) for v in struct.unpack(f"<{frames}h", raw))
        assert peak > 1000, f"{wav_path.name} holds no signal (peak {peak})"

        rows = _index_rows(wav_path.with_suffix(".idx"))
        assert rows, f"{wav_path.name} has an empty index"

        # the index has to describe this file: the spans tile it exactly, in order
        expected_file_pos = 0
        for orig, in_file, count in rows:
            assert (
                in_file == expected_file_pos
            ), f"index gap: expected file position {expected_file_pos}, got {in_file}"
            assert count > 0
            expected_file_pos += count
        assert (
            expected_file_pos == frames
        ), f"index accounts for {expected_file_pos} samples but the WAV holds {frames}"

        # silence was removed, so a transmission can never start earlier in the file than it
        # did in the original audio
        for orig, in_file, _count in rows:
            assert (
                in_file <= orig
            ), f"file position {in_file} ahead of original position {orig}"

        total_frames += frames
        total_rows += rows

    assert (
        total_frames / wave_rate >= MIN_AUDIO_S
    ), f"only {total_frames / wave_rate:.2f}s of audio was kept"

    header = _header_text(wavs[0].with_suffix(".idx"))
    for column in ("orig_sample", "file_sample", "num_samples"):
        assert column in header, f"index header does not explain {column}"

    print(
        f"{len(wavs)} file(s), {total_frames / wave_rate:.2f}s kept, {len(total_rows)} transmission(s)"
    )
