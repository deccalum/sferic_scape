#!/usr/bin/env python3
"""Shared spectral helpers for the tools/ visualisers.

STFT normalisation mirroring the C++ analysis
spectral_envelope.cpp: single-sided Hann magnitude, |X| / (window.sum() / 2)
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

def _ffmpeg_f32(input_args: list[str], sr: int, stdin: bytes | None = None) -> np.ndarray:
    proc = subprocess.run(
        ['ffmpeg', '-y', *input_args, '-f', 'f32le', '-ac', '1', '-ar',
        str(sr), 'pipe:1'],
        input=stdin,
        capture_output=True,
        check=True,
    )
    return np.frombuffer(proc.stdout, dtype=np.float32)

def load_wav(path: Path, sr: int = 48000) -> np.ndarray:
    """Decode an audio file from disk -> float32 mono at sr."""
    return _ffmpeg_f32(['-i', str(path)], sr)

def decode_media_bytes(data: bytes, sr: int) -> np.ndarray:
    """Decode media.audio FLAC bytes (over stdin) -> float32 mono at sr."""
    return _ffmpeg_f32(['-i', 'pipe:0'], sr, stdin=data)

def stft(x: np.ndarray, fft_size: int, hop: int) -> np.ndarray:
    """Hann-windowed STFT -> magnitude (bins × frames), single-sided normalised."""
    window = np.hanning(fft_size).astype(np.float64)
    win_norm = window.sum() / 2.0
    n_frames = max(1, (len(x) - fft_size) // hop + 1)
    S = np.empty((fft_size//2 + 1, n_frames), dtype=np.float32)
    for i in range(n_frames):
        frame = x[i * hop:i*hop + fft_size].astype(np.float64) * window
        S[:, i] = (np.abs(np.fft.rfft(frame)) / win_norm).astype(np.float32)
    return S

def to_db(S: np.ndarray, floor: float = 1e-9) -> np.ndarray:
    return (20.0 * np.log10(np.maximum(S, floor))).astype(np.float32)

def rms_curve(x: np.ndarray, hop: int) -> np.ndarray:
    """Non-overlapping hop-sized block RMS (linear)."""
    n = len(x) // hop
    out = np.empty(n, dtype=np.float32)
    for i in range(n):
        out[i] = float(np.sqrt(np.mean(x[i * hop:(i+1) * hop].astype(np.float64)**2)))
    return out

def show_or_save(fig, path: Path | None) -> None:
    if path is not None:
        path.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(path, dpi=150, bbox_inches='tight')
        print(f'Saved: {path}')
        return
    plt.show()
