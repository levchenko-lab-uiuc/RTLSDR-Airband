# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Working Norms

- **Keep this file updated** as changes are made to the codebase.
- **Never guess or make assumptions** — ask clarifying questions when requirements are unclear.
- **All code changes should include unit tests.** When possible, write tests first and implement code to make them pass (TDD).
- **After writing code, review comments** and remove any that don't explain non-obvious behavior — don't comment what the code already says.

## Development Environment

The repo is set up for development in VS Code using a devcontainer (`.devcontainer/`). When working inside the devcontainer, all compile, test, and run commands must be executed inside the container.

Large system test cases for manual runs can be kept outside the repo in `../RTLSDR-Airband_test_cases/` (a sibling of the checkout, so several checkouts can share one copy). The devcontainer mounts it read-only at `system_tests/test_cases/` (gitignored); the pytest suite does not read it. `initializeCommand` creates an empty folder on the host if it is missing, so the container starts without it. This needs a POSIX host shell (macOS, Linux, WSL) and a container rebuild to take effect.

## Wiki Documentation

User-facing documentation lives in a separate repo: https://github.com/rtl-airband/RTLSDR-Airband/wiki

Flag when code changes require wiki updates and provide suggested content — do not edit the wiki directly.

## Code Review Guidelines

When reviewing code:
- Reference specific files and line numbers (`src/foo.cpp:42`)
- Start with architecture-level concerns before line-level feedback
- Consider SDR/DSP domain context (signal processing constraints, real-time threading, buffer management)
- Verify the testing approach covers the behavior being changed
- Structure feedback clearly: separate blocking issues from suggestions
- Be pragmatic — prefer working correct code over theoretical perfection
- Check for consistency with surrounding code style and conventions

## Project Overview

RTLSDR-Airband is a C++ SDR (Software-Defined Radio) application that receives analog radio voice channels from SDR devices (RTL-SDR, SoapySDR, MiriSDR) and produces MP3 audio streams for Icecast, file recording, UDP, and PulseAudio.

## Build Commands

Dependencies: libconfig++, libmp3lame, libshout, libfftw3f, librtlsdr, libsoapysdr, libpulse. Install via `.github/install_dependencies`.

```bash
# Standard debug build with unit tests
cmake -B builds/Debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNITTESTS=TRUE
cmake --build builds/Debug -j4

# Release build with NFM and SoapySDR
cmake -B builds/Release -DCMAKE_BUILD_TYPE=Release -DNFM=TRUE -DSOAPYSDR=ON
cmake --build builds/Release -j4

# Run unit tests
./builds/Debug/src/unittests
./builds/Release/src/unittests

# Run the binary
./builds/Debug/src/rtl_airband -c /path/to/config.conf
./builds/Release/src/rtl_airband -c /path/to/config.conf
```

Key CMake flags (all in `src/CMakeLists.txt`):

| Flag | Default | Purpose |
|------|---------|---------|
| `NFM` | OFF | Enable Narrow FM demodulation |
| `PLATFORM` | `native` | Optimization target: `native`, `generic`, `rpiv2` |
| `RTLSDR` | ON | RTL-SDR driver |
| `MIRISDR` | ON | Mirics SDR driver |
| `SOAPYSDR` | ON | SoapySDR (vendor-neutral) driver |
| `PULSEAUDIO` | ON | PulseAudio output |
| `BUILD_UNITTESTS` | OFF | Build Google Test unit tests |
| `BCM_VC` | OFF | Broadcom VideoCore GPU FFT (RPi v2 only) |

## Docker

The container image is built from a multi-stage `Dockerfile` based on `alpine:latest` (musl libc). Alpine is used because it ships a native `linux/arm/v6` image, so a single base covers every Raspberry Pi model (Pi 1/Zero armv6 through Pi 5 arm64) — Debian dropped the armel/`arm/v5` variant that the arm/v6 build previously relied on via fallback.

- Build/runtime dependencies come from `apk`. Note the Alpine-specific package names: `libconfig++` (Alpine splits the C++ binding into its own package), `libstdc++` (not in the base image), `fftw-single-libs`, `libpulse`, and `pkgconf` (provides `pkg-config`).
- SoapySDR, the rtl-sdr-blog fork (built from source for RTL-SDR Blog V4 support), and libmirisdr-4 are compiled from source with CMake and installed to `/usr` — musl only searches `/lib` and `/usr/lib`, not `/usr/local/lib`. `ENV CMAKE_POLICY_VERSION_MINIMUM=3.5` lets their pre-3.5 `cmake_minimum_required` configure under Alpine's CMake 4.
- Both stages run `unittests`, so a broken build fails `docker build`. `scripts/find_version` is POSIX `sh` (Alpine has no bash).

```bash
# Build the image for the host architecture
docker build -t rtlsdr-airband .

# Build a specific architecture (requires QEMU/binfmt for cross-arch)
docker buildx build --platform linux/arm/v6 --load -t rtlsdr-airband:armv6 .
```

## Code Formatting and Pre-commit

Uses clang-format v14 with Chromium style (indent=4, column limit=200, config in `.clang-format`).

```bash
# Install pre-commit hooks (once, after cloning)
pre-commit install

# Run all pre-commit hooks manually
pre-commit run --all-files

# Format C++ source files manually (also used by CI)
./scripts/reformat_code
```

Pre-commit hooks (`.pre-commit-config.yaml`) run on every commit and check:
- YAML/JSON validity, trailing whitespace, EOF newlines, shebang permissions, large files, merge conflict markers, private keys
- clang-format on all `src/*.cpp` and `src/*.h` files
- shellcheck on all bash scripts (excluding `init.d/`)
- black, isort, and pylint on all `system_tests/**/*.py` files
- Build (AM and NFM) and C++ unit tests when `src/*.cpp`, `src/*.h`, or `CMakeLists.txt` are modified (`scripts/run_unit_tests`)
- Python system tests when `src/*.cpp`, `src/*.h`, `CMakeLists.txt`, or `system_tests/` are modified (`scripts/run_system_tests`); only runs if the build/unit-test step passes

## CI and Pull Request Checks

Four workflows run checks on open pull requests (`.github/workflows/`); the container build also runs on merges to `main`, tags, and a daily schedule. A fifth, `version_bump.yml`, runs after a PR is merged — see [Version Tagging](#version-tagging).

**`code_formatting.yml`** — runs `./scripts/reformat_code` and fails if any files differ.

**`ci_build.yml`** — builds and tests four configurations on Ubuntu (x86 and ARM) and macOS:
```bash
cmake -B builds/Debug          -DCMAKE_BUILD_TYPE=Debug   -DBUILD_UNITTESTS=TRUE
cmake -B builds/Debug_nfm      -DCMAKE_BUILD_TYPE=Debug   -DNFM=TRUE -DBUILD_UNITTESTS=TRUE
cmake -B builds/Release        -DCMAKE_BUILD_TYPE=Release -DBUILD_UNITTESTS=TRUE
cmake -B builds/Release_nfm    -DCMAKE_BUILD_TYPE=Release -DNFM=TRUE -DBUILD_UNITTESTS=TRUE
```
Then runs `unittests` for all four, installs the Release+NFM build, and smoke-tests `rtl_airband -v`.

**`platform_build.yml`** — builds and tests an AM Release configuration (`PLATFORM=native`) on a Pi 4B runner and an `ubuntu-22.04-arm` runner, then runs unit tests and system tests. (Pi 3B runner is currently disabled.)

**`build_docker_containers.yml`** — builds the multi-arch container image (`linux/amd64`, `386`, `arm64`, `arm/v6`, `arm/v7`), one job per platform via QEMU. Every image is smoke-tested (`rtl_airband -v`) under its target platform. On push/tag/schedule/`workflow_dispatch` the images are pushed by digest to GitHub Container Registry and the `merge` job combines the per-arch digests into a single manifest. Pull requests only build and smoke-test locally (`--load`); a fork PR gets a read-only `GITHUB_TOKEN`, so the registry push is denied. The `Prepare` step's `push` output gates this.

**Before submitting a PR**, the pre-commit hooks cover most checks automatically. For build system or config changes not touching `src/`, verify all four cmake configurations build cleanly by hand.

## Version Tagging

`version_bump.yml` runs when a PR is **merged into `main`** and tags the merge commit with [`anothrNick/github-tag-action@1.64.0`](https://github.com/anothrNick/github-tag-action). Configured with `WITH_V: true` (tags look like `v5.3.0`) and `DEFAULT_BUMP: patch`.

The action scans the **full commit message body of every commit between the previous tag and the merge commit** (`git log "$tag_commit".."$commit" --format=%B`) for a bump keyword, so the keyword can live in any commit in the PR — it does not have to be in the merge commit. "Previous tag" means the highest semver tag, not the most recent one by date:

| Keyword in a commit message | Result |
|------|--------|
| `#major` | `v5.3.0` → `v6.0.0` |
| `#minor` | `v5.3.0` → `v5.4.0` |
| `#patch` | `v5.3.0` → `v5.3.1` |
| `#none` | no tag is created |
| none of the above | `DEFAULT_BUMP: patch` applies → `v5.3.1` |

Rules when writing commit messages:

- **Every merged PR creates a tag** unless a commit says `#none`. With `DEFAULT_BUMP: patch`, doing nothing still bumps the patch version, so add a keyword only to ask for something other than a patch.
- **Add `#minor` for a new user-facing feature or a new config option.** Add `#major` for a breaking change — a removed or renamed config key, or changed default behavior.
- **Highest keyword wins**, checked in the order `#major` → `#minor` → `#patch` → `#none`. One `#major` anywhere in the PR's commits bumps major even if other commits say `#minor`.
- **Matching is a plain substring search over the whole message**, body included. Never write these tokens in prose (for example "fixed a #minor issue") — it will bump the version. Refer to them as "the #minor keyword" only outside commit messages.
- **`#minor` and `#major` also publish a GitHub Release** with generated notes; `#patch` and the default bump only create the tag (`if: steps.tag.outputs.part != 'patch'`).
- Squash-merging collapses the PR's commits into one message, so make sure the keyword survives into the squash message.

After tagging, the workflow re-runs `ci_build.yml`, `platform_build.yml`, and `build_docker_containers.yml` against the new tag (a tag pushed with `GITHUB_TOKEN` does not fire their own `tags: ['v*']` triggers, so they have to be dispatched explicitly).

Note that those three dispatch steps are unguarded: on a `#none` merge the action leaves `new_tag` at the **existing** tag, so they re-run against the previous release and republish its container images.

## System Tests

End-to-end tests live in `system_tests/`. They run the actual binary against generated IQ files and validate the audio output (MP3 duration, rawfile size). Managed with [uv](https://docs.astral.sh/uv/).

```bash
# Run system tests (requires Release binaries — run scripts/run_unit_tests first)
scripts/run_system_tests

# Run manually from the system_tests directory
cd system_tests
uv sync
uv run pytest tests/ \
    --binary ../builds/Release/src/rtl_airband \
    --nfm-binary ../builds/Release_nfm/src/rtl_airband \
    -v
```

Python tooling (formatter, import sorter, linter) is configured in `system_tests/pyproject.toml` under `[tool.black]`, `[tool.isort]`, and `[tool.pylint]`. Run them manually:

```bash
cd system_tests
uv run black .
uv run isort .
uv run pylint conftest.py helpers/ tests/
```

Note that `squelch_lookback` is emitted by `helpers/config_writer.py` but is not yet
part of the `user_provided` JSON schema (`UserChannelCase` in `conftest.py`), so
user-supplied test cases cannot set it.

### Measuring When Audio Was Released

MP3 duration cannot answer *when* a transmission was released: it resolves 72 ms frames, the
encoder is fed whole 125 ms blocks, and it also moves with whatever the audio path does at the
end of a transmission. `helpers/udp_capture.py` takes a channel's `udp_stream` output with
`continuous = true` — raw float32 of `channel->waveout`, audio where the squelch is open and
exact zeros where it is closed — and locates the first released sample exactly.
`config_writer.write_config()` emits that output from a channel's `udp_output` key, and the
capture binds an ephemeral port because the suite runs the binaries in parallel.

`test_squelch_lookback` uses it. The earlier duration-based version was measuring the wrong
thing: it reported ~145 ms of recovery where the real figure is ~12 ms, because an MP3 that
starts one 125 ms block earlier is one block longer whatever the backfill actually did.

## Architecture

### Reception Pipeline

```
SDR device (input-*.cpp)
  → RX thread → circular sample buffer
  → demod thread: FFT (FFTW3) → demod (AM/NFM) → filter → CTCSS → squelch → AGC
  → channel output handlers
  → output thread: MP3 encode (lame) → Icecast / file / UDP / PulseAudio
```

### Key Source Files

| File | Purpose |
|------|---------|
| `src/rtl_airband.cpp` | Main entry point, demod loop, thread management |
| `src/rtl_airband.h` | All major struct/enum definitions (`device_t`, `channel_t`, `mixer_t`, `output_t`) |
| `src/config.cpp` | libconfig++ parsing for devices, channels, mixers, outputs |
| `src/output.cpp` | MP3 encoding, Icecast connections, file/UDP output |
| `src/mixer.cpp` | Multi-channel mixer with ampfactor/balance |
| `src/input-*.cpp` | SDR device drivers (rtlsdr, soapysdr, mirisdr, file) |
| `src/input-common.cpp/h` | Input device abstraction (`input_t` function-pointer interface) |
| `src/filters.cpp/h` | IIR lowpass and notch filters |
| `src/squelch.cpp/h` | Noise-power-based voice activity detection |
| `src/lookback.cpp/h` | Audio delay line that lets squelch open retroactively (`squelch_lookback`) |
| `src/ctcss.cpp/h` | CTCSS tone detection |
| `src/helper_functions.cpp/h` | Path/time helpers, config value coercion, FFT bin placement |

### FFT Bin Placement

`fft_bin_for()` (`src/helper_functions.cpp`) maps a channel frequency to the FFT bin the
demod reads, and `fft_bin_width()` gives the bin width in Hz. Both matter more than they
look:

- The width is `sample_rate / fft_size` in **floating point**. `fft_size` does not divide
  every sample rate evenly — 10 Msps over 2048 bins is 4882.8125 Hz, not 4882 — and
  truncating the remainder drifts by a whole bin for channels far from the centre.
- The bin is the **nearest** one, not the one below. The channel sits wherever it sits
  between two bin centres and the nearer one carries its energy; truncating downwards put
  every channel up to a full bin low, averaging half a bin. Measured on an off-air
  capture, 128.750 MHz (0.88 of the way between bins) reads 1.3 dB stronger in the
  rounded bin than in the one below.

Scan mode tunes 20 bins above the channel to keep it off the DC spike (`config.cpp:446`,
`rtl_airband.cpp:126`), so the channel has to land exactly `fft_size - 20`. That offset is
an exact integer number of bins by construction, which is where a truncating formula is
most obviously wrong — it lands a bin low however the rate and size divide.

### AM DC Blocking

`agcavgfast` sets the AM gain and tracks only upwards, so that it holds the transmission's
level through the noise between transmissions rather than chasing it. Using it as the DC
reference as well put a click at the end of every transmission: the squelch keeps the gate
open for its close delay (197 samples, 24.6 ms) after the carrier has gone, and a held
reference turns `(envelope - agcavgfast)` into a DC block for that whole window. Measured on
an off-air capture, 161 of 165 transmissions ended in a sustained negative DC of
−0.42 to −0.52 lasting a median 14.6 ms, against a `SQUELCH_FADE_MS` ramp of only 2 ms.

`dcavg` (`freq_params_t`) is a separate DC blocker: it follows the envelope **both ways**
over about 4 ms, a corner near 40 Hz, so it leaves speech alone and settles to the noise
before the gate closes. It is charged on open from the same lookback samples as
`agcavgfast`, because otherwise it sits on the inter-transmission noise and puts the same
step at the *start* that it removes from the end.

The gain must not be made to track downwards instead: it is the divisor as well, so
following it down raises the gain on what is left and drove peak `waveout` to 1.0 (clipping)
when tried.

### Silencing the Carrier Drop

The DC blocker removes the gross step but rings on it: the envelope collapses at once, `dcavg`
needs its time constant to follow, and the difference is a step with an exponential tail — a
pop, about −0.13 decaying over 10 ms, audible at the end of most transmissions.

The audio runs behind the squelch, so the delay line already holds what the signal does *after*
the sample being emitted. `AudioLookback::gate_at_lead()` reads the squelch decision
`SQUELCH_FADE_LEAD_MS` (40 ms) ahead and the gate ramps to silence over `SQUELCH_FADE_OUT_MS`
(8 ms), so the audio is already shut when the collapse arrives and the step never reaches the
output. The ramp is raised-cosine rather than linear (`next_fade_level()` returns the position;
`rtl_airband.cpp` shapes it), because a straight ramp still has a corner at each end and a
corner is a click of its own. `SQUELCH_FADE_MS` (5 ms) is the fade in, kept short so a
retroactively recovered onset is not softened.

Four things were tried and rejected, all measured on the same off-air capture:

- **Keying the fade off `Squelch`'s own state.** It only reaches `CLOSING` once its moving
  averages notice, which is after the fact — traced on one transmission the envelope was still
  20 dB over the noise floor when the pop had already happened, collapsing 2 ms later.
- **Keying it off the envelope at the lead.** The envelope crosses the squelch level only once
  the collapse is well under way, which on a strong signal is after the transient; testing it
  against the transmission's own level instead fires on every modulation trough. The gate is
  clean and shuts a fixed close delay after the carrier went, so it is the dependable warning.
- **Ducking to a floor rather than silencing.** Worse both ways: the ringing stays audible under
  the duck, and the residue it lets through is louder.
- **Latching a DC-blocker reset at the collapse** — preset the filter state rather than let it
  ring, the standard answer to a DC-removal filter ringing on a step. It cut the median but left
  *louder* leftovers (fragments peaking at 0.170 against 0.036 without it) and needed two extra
  pieces of state to stop it firing on modulation troughs and on recovered onsets.

Note that `dcavg` is charged on open from the samples *leading* the one being emitted, the same
ones the AGC uses. That looks wrong and is not: the onset is a rising edge, so putting the
reference where the envelope is about to be anticipates the rise. Setting it to the emitted
sample instead was tried and is three times worse, because the reference then lags the whole
way up.

Peak of a 10 ms moving average, the audible thump:

| | at close | at open | mid-speech | audio released |
|---|---|---|---|---|
| before any of this | 0.550 | 0.046 | 0.034 | 1073.9 s |
| `dcavg` DC blocker alone | 0.198 | 0.029 | 0.020 | 1073.9 s |
| + silence keyed off the gate | **0.019** | **0.026** | 0.020 | 1068.0 s |

The close transient ends up at the level of ordinary speech content in the same measure, 30x
down from where it started.

**The fade lead is clamped to the delay line**, so it is only fully available when
`squelch_lookback` is set. At the default of 0 the delay is `AGC_EXTRA` (12.5 ms at 8 kHz) and
the lead is clamped to that, which is why the measurements above use 125 ms. A station that
wants the pop suppression has to set `squelch_lookback`.

### Device Modes

Each device operates in one of two modes, set via `mode = "multichannel"` (default) or `mode = "scan"` in config.

**`R_MULTICHANNEL`** — The SDR is tuned to a fixed center frequency and multiple channels are demodulated simultaneously from the same wideband capture. Each channel has a single `freq` value that must fall within the SDR's bandwidth. This is the common case for monitoring several frequencies at once.

**`R_SCAN`** — The device has exactly one channel, but that channel holds a `freqs` list of frequencies to cycle through. A controller thread monitors the squelch: after ~2 seconds of no signal (10 × 200 ms polls), it retunes the SDR hardware to the next frequency via `input_set_centerfreq()`. When a signal is detected, it stays on the current frequency. Per-frequency settings (labels, `squelch_threshold`/`squelch_snr_threshold`, `modulations`, `notch`/`notch_q`, `ctcss`, `bandwidth`, `ampfactor`) are **parallel lists** to `freqs` (same index = same frequency), not a list of objects — `freqs` is parsed as numbers. Only one channel per device in scan mode. (`rtl_airband.cpp:101-140`, `config.cpp:312-729`)

### Threading Model

- **RX thread** (1 per device, always) — reads SDR samples into the circular buffer (`input-common.cpp`)
- **Controller thread** (1 per device, `R_SCAN` mode only) — scanning/squelch state machine for devices that scan across frequencies; not created for `R_MULTICHANNEL` devices (`rtl_airband.cpp:1005-1013`)
- **Demod thread** (1 total by default; 1 per device if `multiple_demod_threads=true` in config) — FFT, demodulation, filter, CTCSS, squelch, AGC for all assigned devices (`rtl_airband.cpp:1044`)
- **Output thread** (1 total by default; 1 per device + 1 for mixers if `multiple_output_threads=true`) — MP3 encoding and streaming
- **Mixer thread** (1 total, only if any mixers are configured) — processes all mixers; not per-mixer (`rtl_airband.cpp:1091-1092`)

### Configuration Format

Config files use libconfig++ syntax. Sample configs in `config/`. Top-level sections:

```
devices: ( { type = "rtlsdr"; centerfreq = 120.0; gain = 25;
             channels: ( { freq = 119.5; modulation = "am";
                           outputs: ( { type = "icecast"; ... } ); } ); } );
mixers: { mix1: { outputs: ( { type = "icecast"; ... } ); } };
```

### Squelch Lookback

Squelch cannot tell a transmission has started until the level has risen above the threshold and survived the open delay, so the first fraction of a second is cut off. `AudioLookback` (`src/lookback.cpp/h`) runs the AM audio path a fixed number of samples behind the squelch, keeping ring buffers of the signal envelope and of the squelch decision for each sample. When squelch opens, `open_retroactively()` walks back through the stored envelope to where the signal actually started — the most recent 3 ms window whose **mean** is below the geometric mean of the noise floor and the squelch level — and marks those not-yet-emitted samples open, so the transmission is released from its true start. Reaching back only as far as the signal extends, rather than a fixed pre-roll, keeps a long delay from prepending noise to every transmission.

The stopping test is on the window's mean, not on every sample in it. An AM envelope fluctuates several dB from sample to sample, so a window in which *every* sample falls below the threshold is uncommon even in pure noise: measured on an off-air capture, one occurs roughly every 270 ms at the threshold this is called with. The original implementation required one, which meant the walk-back usually found no stopping point in the noise ahead of a transmission and ran until the delay line was exhausted — turning the budget into exactly the fixed pre-roll it is meant not to be. On real traffic that released a burst of noise ahead of 26 of 43 transmissions, and because the AGC still holds the previous transmission's level between transmissions, AM demodulated that burst into a near full-scale DC step louder than the speech it was supposed to recover. Testing the mean fixed it: noise released per transmission went from a median of 13.5 ms to 0.0 ms, and peak `waveout` on the busiest channel from 1.00 back to 0.80, the same as with lookback disabled. See `LookbackTest.backfill_stops_in_a_fluctuating_noise_floor` for the regression test. A later 600 s capture confirms the shipped behaviour independently: across 145 transmissions on nine channels, lookback recovers a median 17.9 ms of onset (p90 41.6 ms) that a lookback of 0 cuts off, for a median 3.2 ms of noise released and no increase in peak `waveout`. 125 ms and 250 ms perform identically except on the slowest-rising transmissions, so 125 ms buys the same recovery for half the output delay.

The per-channel (or per-frequency, as a parallel list in scan mode) `squelch_lookback` option sets the budget in milliseconds, 0-250, default 0. The delay line always holds at least `AGC_EXTRA` samples, which is the lead the AGC has always had over the audio, so a lookback of 0 leaves the timing of the audio path as it has always been. Notes:

- **AM only.** NFM demodulates from the current I/Q sample rather than from the delay line; a non-zero `squelch_lookback` on an NFM channel is a config error.
- The whole audio stream is delayed by the configured amount, so Icecast/file output runs that much later. Raw I/Q output is *not* delayed (config warns), and mixing channels with different values skews them (config warns).
- The backfill is keyed off `Squelch::is_open()`, not the state machine's `first_open_sample()`, so a squelch held closed by CTCSS does not release buffered audio.
- In scan mode the delay line is dropped on retune, since what it holds belongs to the previous frequency.

Mixers have **no `inputs` section** — a channel connects to a mixer implicitly via an output `type = "mixer"` + `name` (+ optional `ampfactor`, `balance`), see `mixer_connect_input` (`src/config.cpp:168-193`, `src/mixer.cpp:57`).

Output types: `icecast`, `file`, `rawfile`, `udp_stream`, `mixer`, `pulse`.

### MP3 Encoder Settings

`airlame_init()` (`src/output.cpp`) took its bitrate, quality and VBR mode as literals chosen for
streaming many channels to Icecast over a thin uplink. They are config options now:

| option | default | meaning |
|---|---|---|
| `mp3_bitrate` | 16 | kbps, 8-320 |
| `mp3_quality` | 7 | LAME algorithm quality, 0 best to 9 fastest |
| `mp3_vbr` | true | `vbr_mtrh`; false selects constant bitrate |

Settable globally or per channel (and per mixer), the channel value winning. The defaults are the
values that were compiled in, so an existing config encodes byte for byte as before -
`test_mp3_defaults_are_unchanged` asserts exactly that.

With VBR the bitrate is a target the encoder may fall below on quiet passages; with
`mp3_vbr = false` it is held constant, which is what makes a size comparison meaningful.
`valid_mp3_settings()` rejects out of range values at parse time rather than leaving
`lame_init_params()` to fail, since that failure would otherwise leave the channel running with
no encoder and no explanation.

The output sample rate stays at `MP3_RATE` (8000): `output.cpp:114` also reports it to Icecast as
stream metadata, so making it per channel would need that path changed too.

`test_mp3_options` asserts on encoded size rather than on the frame header, because a bitrate that
is parsed and then dropped still yields a valid MP3 with a plausible header.

### Unit Tests

Tests use Google Test (fetched via CMake FetchContent). Test files in `src/test_*.cpp` cover filters, squelch, CTCSS, helper functions, and signal generation. `src/test_base_class.h` provides test utilities.
