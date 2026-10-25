"""
Capture a channel's audio from an rtl_airband udp_stream output.

MP3 duration can only resolve 72 ms frames, and the encoder is fed whole 125 ms
blocks, so a test that needs to know *when* audio was released cannot use it. A
`continuous` udp_stream carries channel->waveout as raw float32 for the whole run -
audio where the squelch is open, exact zeros where it is closed - which locates the
first released sample exactly.
"""

from __future__ import annotations

import socket
import threading

import numpy as np


class UdpAudioCapture:
    """Receive float32 audio on a UDP port for the lifetime of the context.

    Binds before the config naming the port is written, so `port` is known and the
    socket is already listening by the time rtl_airband starts sending.
    """

    def __init__(self, port: int = 0, host: str = "127.0.0.1") -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 16 << 20)
        self._sock.bind((host, port))
        # Port 0 by default: the tests run in parallel, so a fixed one collides.
        self.port: int = self._sock.getsockname()[1]
        self._sock.settimeout(0.5)
        self._chunks: list[bytes] = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                self._chunks.append(self._sock.recv(65536))
            except socket.timeout:
                continue

    def __enter__(self) -> "UdpAudioCapture":
        self._thread.start()
        return self

    def __exit__(self, *_exc: object) -> None:
        self._stop.set()
        self._thread.join(timeout=5.0)
        self._sock.close()

    def samples(self) -> np.ndarray:
        return np.frombuffer(b"".join(self._chunks), dtype="<f4")


def first_released_sample(
    samples: np.ndarray, wave_rate: int, min_run_s: float = 0.5
) -> int:
    """Index of the first sample of the first released run longer than min_run_s.

    Skips the short burst the delay line emits as it fills at start of stream, which
    is not a transmission.
    """
    flags = (samples != 0.0).astype(np.int8)
    edges = np.diff(np.concatenate([[0], flags, [0]]))
    starts = np.flatnonzero(edges == 1)
    ends = np.flatnonzero(edges == -1)
    for start, end in zip(starts, ends):
        if end - start >= int(min_run_s * wave_rate):
            return int(start)
    raise AssertionError("no released audio run found in the captured stream")
