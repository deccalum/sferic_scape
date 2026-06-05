#!/usr/bin/env python3
"""Deep diagnostic comparison: recording vs spectral vs synthesized.

Panels:
  Row 0  — spectrograms (shared colorscale, matched duration)
  Row 1  — difference spectrograms  rec–spectral  |  spectral–syn  |  rec–syn
  Row 2  — mean power spectrum overlay (all 3)  |  overlaid RMS envelopes
  Row 3  — per-octave-band RMS error  (rec vs syn, 6 bands)

Usage:
    python3 tools/diagnose.py data/out/<path>/ --save data/out/<path>/<filename>.png
"""

import argparse
import subprocess
from pathlib import Path

import numpy as np
import matplotlib
import matplotlib.ticker
import matplotlib.gridspec as gridspec
import matplotlib.pyplot as plt


def load_wav(path: Path, target_sr: int = 48000) -> np.ndarray:
    proc = subprocess.run(
        ['ffmpeg', '-y', '-i', str(path),
         '-f', 'f32le', '-ac', '1', '-ar', str(target_sr), 'pipe:1'],
        capture_output=True, check=True,
    )
    return np.frombuffer(proc.stdout, dtype=np.float32)

def detect_source(env_file: Path = Path('.env')) -> Path | None:
    for line in env_file.read_text(encoding='utf-8').splitlines():
        if line.startswith('SFERIC_INPUT='):
            return Path(line.split('=', 1)[1].strip())
    return None


def stft(x: np.ndarray, fft_size: int, hop: int) -> np.ndarray:
    """Hann-windowed STFT → magnitude array (bins × frames), normalised."""
    window   = np.hanning(fft_size).astype(np.float64)
    win_norm = window.sum() / 2.0
    n_frames = max(1, (len(x) - fft_size) // hop + 1)
    S = np.empty((fft_size // 2 + 1, n_frames), dtype=np.float32)
    for i in range(n_frames):
        frame   = x[i * hop : i * hop + fft_size].astype(np.float64) * window
        S[:, i] = (np.abs(np.fft.rfft(frame)) / win_norm).astype(np.float32)
    return S

def to_db(S: np.ndarray, floor: float = 1e-9) -> np.ndarray:
    return 20.0 * np.log10(np.maximum(S, floor)).astype(np.float32)

def rms_curve(x: np.ndarray, hop: int) -> np.ndarray:
    n = len(x) // hop
    out = np.empty(n, dtype=np.float32)
    for i in range(n):
        out[i] = float(np.sqrt(np.mean(x[i * hop : (i + 1) * hop].astype(np.float64) ** 2)))
    return out

def align(a: np.ndarray, b: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    n = min(len(a), len(b))
    return a[:n], b[:n]

def align3(a: np.ndarray, b: np.ndarray, c: np.ndarray):
    n = min(len(a), len(b), len(c))
    return a[:n], b[:n], c[:n]

def trim_frames(*arrays: np.ndarray) -> tuple[np.ndarray, ...]:
    """Truncate 2-D (bins×frames) spectrograms to the shortest frame count."""
    n = min(a.shape[1] for a in arrays)
    return tuple(a[:, :n] for a in arrays)


_OCTAVES = [
    (20,   80,   '20–80 Hz'),
    (80,   250,  '80–250 Hz'),
    (250,  500,  '250–500 Hz'),
    (500,  2000, '0.5–2 kHz'),
    (2000, 4000, '2–4 kHz'),
    (4000, 8000, '4–8 kHz'),
]

def band_rms(S: np.ndarray, freq_hz: np.ndarray, flo: float, fhi: float) -> np.ndarray:
    mask = (freq_hz >= flo) & (freq_hz < fhi)
    return np.sqrt(np.mean(S[mask] ** 2, axis=0))


_CMAP_SPEC  = 'magma'
_CMAP_DIFF  = 'RdBu_r'   # red = rec has more, blue = syn has more
_COLORS     = {'rec': '#e07b39', 'spc': '#5ba5c8', 'syn': '#6abf69'}
_F_MIN      = 20.0

def _spec_ax(ax: plt.Axes, t: np.ndarray, freq: np.ndarray, S_db: np.ndarray,
             vmin: float, vmax: float, fmax: float,
             title: str = '', ylabel: bool = True, cmap=_CMAP_SPEC):
    f    = freq[1:]
    S    = S_db[1:, :]
    mask = f <= fmax
    im   = ax.pcolormesh(t, f[mask], S[mask, :], cmap=cmap,
                         vmin=vmin, vmax=vmax, shading='auto', rasterized=True)
    ax.set_yscale('log')
    ax.set_ylim(_F_MIN, fmax)
    ax.yaxis.set_major_formatter(
        matplotlib.ticker.FuncFormatter(lambda v, _: f'{int(v):,}'))
    if title:
        ax.set_title(title, fontsize=9, fontweight='bold')
    if ylabel:
        ax.set_ylabel('Hz', fontsize=8)
    else:
        ax.set_yticklabels([])
    ax.tick_params(labelsize=7)
    return im


def main() -> None:
    ap = argparse.ArgumentParser(description='Diagnostic comparison figure.')
    ap.add_argument('out_dir',  type=Path)
    ap.add_argument('--source', type=Path, default=None)
    ap.add_argument('--fft',    type=int,   default=2048)
    ap.add_argument('--hop',    type=int,   default=512)
    ap.add_argument('--fmax',   type=float, default=8000.0)
    ap.add_argument('--save',   type=Path,  default=None)
    args = ap.parse_args()

    out_dir    = args.out_dir
    sourc_path = args.source
    spect_path = out_dir / 'spectral.wav'
    synth_path = out_dir / 'synth.wav'
    sr         = 48000
    fft        = args.fft
    hop        = args.hop

    print('Loading ...')
    rec = load_wav(sourc_path)
    spc = load_wav(spect_path)
    syn = load_wav(synth_path)

    print('Computing STFTs ...')
    S_rec = stft(rec, fft, hop)
    S_spc = stft(spc, fft, hop)
    S_syn = stft(syn, fft, hop)

    freq_hz = np.fft.rfftfreq(fft, d=1.0 / sr).astype(np.float32)

    S_rec, S_spc, S_syn = trim_frames(S_rec, S_spc, S_syn)
    n_frames = S_rec.shape[1]
    t_s      = (np.arange(n_frames) * hop / sr).astype(np.float32)
    S_rec_db = to_db(S_rec)
    S_spc_db = to_db(S_spc)
    S_syn_db = to_db(S_syn)
    fmax_idx = int(np.searchsorted(freq_hz, args.fmax))
    vmax = float(S_rec_db[1:fmax_idx].max())
    vmin = vmax - 80.0

    D_rs  = S_rec_db - S_spc_db   # rec – spectral
    D_ss  = S_spc_db - S_syn_db   # spectral – syn
    D_rsy = S_rec_db - S_syn_db   # rec – syn (total error)

    dlim = 20.0  # ±20 dB range

    rms_rec = rms_curve(rec[:n_frames * hop], hop)
    rms_spc = rms_curve(spc[:n_frames * hop], hop)
    rms_syn = rms_curve(syn[:n_frames * hop], hop)
    t_rms   = np.arange(len(rms_rec)) * hop / sr

    mean_rec = to_db(np.sqrt(np.mean(S_rec ** 2, axis=1)))
    mean_spc = to_db(np.sqrt(np.mean(S_spc ** 2, axis=1)))
    mean_syn = to_db(np.sqrt(np.mean(S_syn ** 2, axis=1)))

    fig = plt.figure(figsize=(16, 18))
    fig.suptitle(f'{sourc_path.stem} — diagnostic', fontsize=13, fontweight='bold', y=0.99)

    gs = gridspec.GridSpec(
        4, 3,
        figure=fig,
        height_ratios=[2.2, 2.2, 1.6, 1.6],
        hspace=0.38, wspace=0.07,
    )

    ax_r0 = [fig.add_subplot(gs[0, c]) for c in range(3)]
    for ax, title, S_db in zip(ax_r0,
                                ['Recording', 'Spectral', 'Synthesized'],
                                [S_rec_db, S_spc_db, S_syn_db]):
        im0 = _spec_ax(ax, t_s, freq_hz, S_db, vmin, vmax,
                       args.fmax, title=title, ylabel=(ax is ax_r0[0]))

    cb0 = fig.colorbar(im0, ax=ax_r0, label='Magnitude (dB)',
                       pad=0.01, fraction=0.012)
    cb0.ax.tick_params(labelsize=7)

    ax_r1 = [fig.add_subplot(gs[1, c]) for c in range(3)]
    diff_titles = [
        'rec − spectral  (analysis loss)',
        'spectral − syn  (param loss)',
        'rec − syn  (total error)',
    ]
    for ax, title, D in zip(ax_r1, diff_titles, [D_rs, D_ss, D_rsy]):
        im1 = _spec_ax(ax, t_s, freq_hz, D, -dlim, dlim,
                       args.fmax, title=title, ylabel=(ax is ax_r1[0]),
                       cmap=_CMAP_DIFF)

    cb1 = fig.colorbar(im1, ax=ax_r1, label='dB difference (red = reference louder)',
                       pad=0.01, fraction=0.012)
    cb1.ax.tick_params(labelsize=7)

    ax_mps = fig.add_subplot(gs[2, :2])
    f_plot = freq_hz[1:fmax_idx]
    for label, color, m in [('Recording',   _COLORS['rec'], mean_rec),
                            ('Spectral',    _COLORS['spc'], mean_spc),
                            ('Synthesized', _COLORS['syn'], mean_syn)]:
        ax_mps.plot(f_plot, m[1:fmax_idx], label=label, color=color,
                    linewidth=1.2, alpha=0.9)
    ax_mps.set_xscale('log')
    ax_mps.set_xlim(_F_MIN, args.fmax)
    ax_mps.set_xlabel('Frequency (Hz)', fontsize=8)
    ax_mps.set_ylabel('Mean magnitude (dB)', fontsize=8)
    ax_mps.set_title('Mean power spectrum', fontsize=9, fontweight='bold')
    ax_mps.legend(fontsize=8, loc='upper right')
    ax_mps.grid(True, which='both', alpha=0.2, linewidth=0.5)
    ax_mps.tick_params(labelsize=7)
    ax_mps.xaxis.set_major_formatter(
        matplotlib.ticker.FuncFormatter(lambda v, _: f'{int(v):,}'))

    ax_rms = fig.add_subplot(gs[2, 2])
    for label, color, r in [('Recording',   _COLORS['rec'], rms_rec),
                            ('Spectral',    _COLORS['spc'], rms_spc),
                            ('Synthesized', _COLORS['syn'], rms_syn)]:
        ax_rms.plot(t_rms, 20.0 * np.log10(r + 1e-9),
                    label=label, color=color, linewidth=1.0, alpha=0.9)
    ax_rms.set_xlabel('Time (s)', fontsize=8)
    ax_rms.set_ylabel('RMS (dB)', fontsize=8)
    ax_rms.set_title('RMS envelope', fontsize=9, fontweight='bold')
    ax_rms.legend(fontsize=7, loc='lower right')
    ax_rms.grid(True, alpha=0.2, linewidth=0.5)
    ax_rms.tick_params(labelsize=7)

    ax_bands = [fig.add_subplot(gs[3, c]) for c in range(3)]
    bands_per_ax = 2
    for col in range(3):
        ax = ax_bands[col]
        lines = []
        for b_idx in range(bands_per_ax):
            band_i = col * bands_per_ax + b_idx
            if band_i >= len(_OCTAVES):
                break
            flo, fhi, label = _OCTAVES[band_i]
            r_rec = 20.0 * np.log10(band_rms(S_rec, freq_hz, flo, fhi) + 1e-9)
            r_syn = 20.0 * np.log10(band_rms(S_syn, freq_hz, flo, fhi) + 1e-9)
            diff  = r_rec - r_syn
            style = '-' if b_idx == 0 else '--'
            ln,   = ax.plot(t_s, diff, linewidth=0.9, linestyle=style, label=label)
            lines.append(ln)

        ax.axhline(0, color='k', linewidth=0.6, alpha=0.4)
        ax.set_ylim(-25, 25)
        ax.set_xlabel('Time (s)', fontsize=8)
        if col == 0:
            ax.set_ylabel('rec−syn (dB)', fontsize=8)
        ax.set_title('Per-band error (rec−syn, positive = syn too quiet)',
                     fontsize=8, fontweight='bold')
        ax.legend(fontsize=7, loc='upper right')
        ax.grid(True, alpha=0.2, linewidth=0.5)
        ax.tick_params(labelsize=7)

    if args.save:
        plt.savefig(args.save, dpi=150, bbox_inches='tight')
        print(f'Saved: {args.save}')
    else:
        plt.show()


if __name__ == '__main__':
    main()
