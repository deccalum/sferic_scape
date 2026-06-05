#!/usr/bin/env python3
"""Side-by-side spectrogram comparison: recording | spectral | synthesized.

Usage:
    python3 tools/compare.py data/out/<path>/
    python3 tools/compare.py data/out/<path>/ --source <filename>
    python3 tools/compare.py data/out/<path>/ --save <filename>
"""

import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np
import matplotlib
import matplotlib.ticker
import matplotlib.pyplot as plt


def load_wav(path: Path, target_sr: int = 48000) -> np.ndarray:
    """Read any WAV via ffmpeg → float32 mono array at target_sr."""
    proc = subprocess.run(
        [
        'ffmpeg', '-y', '-i',
        str(path), '-f', 'f32le', '-ac', '1', '-ar',
        str(target_sr), 'pipe:1'
        ],
        capture_output=True,
        check=True,
    )
    return np.frombuffer(proc.stdout, dtype=np.float32)


def stft_spectrogram(
    x: np.ndarray,
    sr: int,
    fft_size: int = 2048,
    hop: int = 512,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Hann-windowed STFT → (freq_hz, time_s, magnitude_db).

    Magnitude is normalised by window_sum/2 so that a sine of amplitude A gives
    S ≈ A in linear scale — mirrors the analysis STFT normalisation.
    """
    window = np.hanning(fft_size).astype(np.float64)
    win_norm = window.sum() / 2.0
    n_frames = max(1, (len(x) - fft_size) // hop + 1)

    S = np.empty((fft_size//2 + 1, n_frames), dtype=np.float32)
    for i in range(n_frames):
        frame = x[i * hop:i*hop + fft_size].astype(np.float64) * window
        S[:, i] = (np.abs(np.fft.rfft(frame)) / win_norm).astype(np.float32)

    freq_hz = np.fft.rfftfreq(fft_size, d=1.0 / sr).astype(np.float32)
    time_s = (np.arange(n_frames) * hop / sr).astype(np.float32)
    S_db = (20.0 * np.log10(S + 1e-9)).astype(np.float32)

    return freq_hz, time_s, S_db


def rms_envelope(x: np.ndarray, sr: int, hop: int = 512) -> tuple[np.ndarray, np.ndarray]:
    """Return (time_s, rms_db) computed in non-overlapping hop-sized blocks."""
    n_frames = len(x) // hop
    rms = np.zeros(n_frames, dtype=np.float32)
    for i in range(n_frames):
        block = x[i * hop:(i+1) * hop]
        rms[i] = float(np.sqrt(np.mean(block.astype(np.float64)**2)))
    time_s = (np.arange(n_frames) * hop / sr).astype(np.float32)
    rms_db = 20.0 * np.log10(rms + 1e-9)
    return time_s, rms_db


def detect_source(env_file: Path = Path('.env')) -> Path | None:
    if not env_file.exists():
        return None
    for line in env_file.read_text(encoding='utf-8').splitlines():
        if line.startswith('SFERIC_INPUT='):
            return Path(line.split('=', 1)[1].strip())
    return None


_CMAP = 'magma'
_F_MIN = 20.0                          # Hz — log scale floor

def _plot_column(
    ax_spec: plt.Axes, ax_rms: plt.Axes, title: str, freq_hz: np.ndarray, time_s: np.ndarray,
    S_db: np.ndarray, t_rms: np.ndarray, rms_db: np.ndarray, vmin: float, vmax: float, fmax: float,
    show_ylabel: bool
) -> matplotlib.collections.QuadMesh:

    # Skip DC bin (freq=0) — incompatible with log y-axis.
    f = freq_hz[1:]
    S = S_db[1:, :]
    mask = f <= fmax
    im = ax_spec.pcolormesh(
        time_s, f[mask], S[mask], cmap=_CMAP, vmin=vmin, vmax=vmax, shading='auto', rasterized=True
    )
    ax_spec.set_title(title, fontsize=10, fontweight='bold')
    ax_spec.set_yscale('log')
    ax_spec.set_ylim(_F_MIN, fmax)
    ax_spec.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f'{int(v):,}'))
    if show_ylabel:
        ax_spec.set_ylabel('Frequency (Hz)')
    ax_spec.set_xlabel('')

    ax_rms.plot(t_rms, rms_db, linewidth=0.9, color='#4a9eda')
    ax_rms.set_ylim(-70, 3)
    ax_rms.set_xlabel('Time (s)', fontsize=8)
    if show_ylabel:
        ax_rms.set_ylabel('RMS (dB)', fontsize=8)
    ax_rms.tick_params(labelsize=7)
    ax_rms.grid(True, alpha=0.25, linewidth=0.5)

    return im


def main() -> None:
    ap = argparse.ArgumentParser(
        description='Compare recording | spectral | synthesized spectrograms.'
    )
    ap.add_argument(
        'out_dir', type=Path, help='sferic output directory containing spectral.wav and synth.wav'
    )
    ap.add_argument(
        '--source', type=Path, default=None, help='Source WAV (auto-detected from .env if omitted)'
    )
    ap.add_argument('--fft', type=int, default=2048, help='STFT FFT size (default 2048)')
    ap.add_argument('--hop', type=int, default=512, help='STFT hop size (default 512)')
    ap.add_argument(
        '--fmax', type=float, default=8000.0, help='Max frequency to display in Hz (default 8000)'
    )
    ap.add_argument(
        '--vmin', type=float, default=-80.0, help='Spectrogram floor in dB (default -80)'
    )
    ap.add_argument(
        '--save', type=Path, default=None, help='Save figure to path instead of displaying'
    )
    args = ap.parse_args()

    out_dir = args.out_dir
    if not out_dir.is_dir():
        sys.exit(f'Not a directory: {out_dir}')

    spectral_path = out_dir / 'spectral.wav'
    synth_path    = out_dir / 'synth.wav'

    if not synth_path.exists():
        sys.exit(f'synth.wav not found in {out_dir} — run sferic first')

    source_path = args.source or detect_source()
    if source_path is None:
        sys.exit('No source WAV specified. Use --source or set SFERIC_INPUT in .env')
    if not source_path.exists():
        sys.exit(f'Source WAV not found: {source_path}')

    has_spectral = spectral_path.exists()
    if not has_spectral:
        print(
            f'Note: {spectral_path.name} not found — showing recording + synthesized only',
            file=sys.stderr
        )

    print('Loading audio ...')
    recording   = load_wav(source_path)
    synthesized = load_wav(synth_path)
    spectral    = load_wav(spectral_path) if has_spectral else None
    sr = 48000

    print('Computing spectrograms ...')
    freq, t_rec, S_rec = stft_spectrogram(recording, sr, args.fft, args.hop)
    _, t_syn, S_syn = stft_spectrogram(synthesized, sr, args.fft, args.hop)
    if spectral is not None:
        _, t_spc, S_spc = stft_spectrogram(spectral, sr, args.fft, args.hop)

    fmax_idx = np.searchsorted(freq, args.fmax)
    vmax = float(S_rec[1:fmax_idx].max())
    vmin = args.vmin

    t_rms_rec, rms_rec = rms_envelope(recording, sr, args.hop)
    t_rms_syn, rms_syn = rms_envelope(synthesized, sr, args.hop)
    if spectral is not None:
        t_rms_spc, rms_spc = rms_envelope(spectral, sr, args.hop)

    columns = [('Recording', freq, t_rec, S_rec, t_rms_rec, rms_rec)]
    if has_spectral:
        columns.append(('Spectral', freq, t_spc, S_spc, t_rms_spc, rms_spc))
    columns.append(('Synthesized', freq, t_syn, S_syn, t_rms_syn, rms_syn))

    n_cols = len(columns)
    fig, axes = plt.subplots(
        2,
        n_cols,
        figsize=(5 * n_cols, 7),
        gridspec_kw={
        'height_ratios': [3, 1],
        'hspace': 0.08,
        'wspace': 0.06
        },
    )
    fig.suptitle(f'{source_path.stem}', fontsize=12, fontweight='bold', y=0.98)

    last_im = None
    for col, (title, f_hz, t_s, S_db, t_r, r_db) in enumerate(columns):
        last_im = _plot_column(
            axes[0, col],
            axes[1, col],
            title=title,
            freq_hz=f_hz,
            time_s=t_s,
            S_db=S_db,
            t_rms=t_r,
            rms_db=r_db,
            vmin=vmin,
            vmax=vmax,
            fmax=args.fmax,
            show_ylabel=(col == 0),
        )
        if col > 0:
            axes[0, col].set_yticklabels([])
            axes[1, col].set_yticklabels([])

    cbar = fig.colorbar(
        last_im, ax=axes[0, :].tolist(), label='Magnitude (dB)', pad=0.01, fraction=0.015
    )
    cbar.ax.tick_params(labelsize=8)

    if args.save:
        plt.savefig(args.save, dpi=150, bbox_inches='tight')
        print(f'Saved: {args.save}')
    else:
        plt.show()

if __name__ == '__main__':
    main()
