#!/usr/bin/env python3
"""Sandbox for the thunder rumble-end algorithm.

  1. detect clap attacks from the dB envelope (rise over a trailing floor),
     optionally dropping attacks whose peak clears less than --min-prominence of
     the recording's dynamic range (99th−5th pct),
  2. sample the pre-attack floor just before each attack,
  3. when there are >= 2 claps, set a gate (under-median-mean by default),
  4. close each rumble at the first of: gate hold, level-plateau listener, or
     cap,
  5. reject short-for-loudness windows via auto valley on rumble/prominence.

    python3 tools/noise_gate.py <id> --save path/file.png
    python3 tools/noise_gate.py <id> --cpp --save path/file.png
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib
import matplotlib.pyplot as plt
import matplotlib.ticker
import numpy as np
from matplotlib.lines import Line2D
from sqlalchemy import select

_REPO_ROOT = Path(__file__).resolve().parents[1]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from spectral_common import decode_media_bytes, show_or_save, stft, to_db # noqa: E402
from db.session import SessionLocal                                       # noqa: E402
from db.models import DetectorRun, Exemplar, Media, MediaOrigin           # noqa: E402

# Defaults mirror src/module/thunder/detector.h DetectorConfig + analysis::OnsetConfig
_THUNDER_CEIL_HZ = 230.0               # thunder_constants.h FUNDAMENTAL_CEIL_HZ
_RUMBLE_MAX_S = 30.0                   # hard cap on a rumble
_PEAK_WIN_S = 1.0                      # peak search window after onset
_ENV_HOP_S = 0.010                     # broadband envelope frame hop
_ENV_SMOOTH_S = 0.050                  # envelope moving-average window
_CMAP = 'magma'
_F_MIN = 20.0
_C_EXPECTED = '#4ad66d'
_C_ONSET = '#e5484d'
_C_GATE = '#3ba7c4'
_C_REJECT = '#6b7280'
_C_CPP = '#8b5cf6'
_END_COLORS = {'gate': '#f5a524', 'level': '#d65db1', 'cap': '#9aa0a6'}

def _ratio_or_auto(s: str) -> float | str:
    return s if s == 'auto' else float(s)

def _label(i: int) -> str:
    s = ''
    i += 1
    while i:
        i, r = divmod(i - 1, 26)
        s = chr(97 + r) + s
    return s

def envelope_db(x: np.ndarray, hop: int, smooth: int) -> np.ndarray:
    n = len(x) // hop
    blocks = x[:n * hop].reshape(n, hop).astype(np.float64)
    rms = np.sqrt(np.maximum(np.mean(blocks**2, axis=1), 1e-12))
    db = 20.0 * np.log10(rms)
    if smooth > 1:
        db = np.convolve(db, np.ones(smooth) / smooth, mode='same')
    return db

def trailing_percentile(a: np.ndarray, win: int, q: float) -> np.ndarray:
    if win <= 1:
        return a.copy()
    view = np.lib.stride_tricks.sliding_window_view(a, win) # window ends at i+win-1
    p = np.percentile(view, q, axis=1)
    out = np.empty_like(a)
    out[win - 1:] = p
    out[:win - 1] = p[0]
    return out

def slope_at(env_db: np.ndarray, f: int, win: int, hop_s: float) -> float:
    seg = env_db[f:f + win]
    if len(seg) < 2:
        return 0.0
    t = np.arange(len(seg)) * hop_s
    t -= t.mean()
    return float(np.dot(t, seg - seg.mean()) / np.dot(t, t))

def detect_attacks(
    env_db: np.ndarray, hop_s: float, rise_db: float, lookback_s: float, attack_s: float,
    refractory_s: float
) -> tuple[list[int], np.ndarray]:
    win = max(1, round(lookback_s / hop_s))
    floor = trailing_percentile(env_db, win, 10.0)
    open_level = floor + rise_db
    hold = max(1, round(attack_s / hop_s))
    refractory = round(refractory_s / hop_s)

    attacks: list[int] = []
    last = -refractory
    n = len(env_db)
    i = 0
    while i < n:
        if env_db[i] > open_level[i] and (i - last) >= refractory:
            if np.all(env_db[i:i + hold] > open_level[i:i + hold]):
                attacks.append(i)
                last = i
                i += refractory
                continue
        i += 1
    return attacks, floor

def pre_attack_floors(
    env_db: np.ndarray, attacks: list[int], hop_s: float, lookback_s: float, guard_s: float
) -> list[float]:
    win = round(lookback_s / hop_s)
    guard = round(guard_s / hop_s)
    out = []
    for a in attacks:
        lo = max(0, a - win - guard)
        hi = max(lo + 1, a - guard)
        out.append(float(np.median(env_db[lo:hi])))
    return out

def prominence_gate(
    env_db: np.ndarray, attacks: list[int], floors: list[float], hop_s: float, min_frac: float
) -> tuple[list[int], list[float]]:
    if min_frac <= 0:
        return attacks, floors
    dyn = float(np.percentile(env_db, 99) - np.percentile(env_db, 5))
    bar = min_frac * dyn
    peak_win = max(1, round(_PEAK_WIN_S / hop_s))
    n = len(env_db)
    keep_a, keep_f = [], []
    for a, fl in zip(attacks, floors):
        peak = float(np.max(env_db[a:min(n, a + peak_win)]))
        if peak - fl >= bar:
            keep_a.append(a)
            keep_f.append(fl)
    return keep_a, keep_f

def compute_gate(
    env_db: np.ndarray, floors: list[float], n_attacks: int, mode: str
) -> float | None:
    if n_attacks < 2:
        return None
    if mode == 'under-median-mean':
        below = env_db[env_db < float(np.median(env_db))]
        return float(below.mean()) if below.size else float(np.median(env_db))
    return float(np.median(floors))

class Window:
    __slots__ = (
        'onset', 'end', 'closed_by', 'armed', 'peak_db', 'close_slope', 'close_settle',
        'prominence', 'rejected'
    )

    def __init__(
        self,
        onset: int,
        end: int,
        closed_by: str,
        armed: int | None,
        peak_db: float,
        close_slope: float | None = None,
        close_settle: float | None = None,
        prominence: float = 0.0,
        rejected: bool = False
    ):
        self.onset = onset
        self.end = end
        self.closed_by = closed_by                         # 'gate' | 'level' | 'cap'
        self.armed = armed                                 # frame the release listener armed, or None
        self.peak_db = peak_db
        self.close_slope = close_slope                     # dB/s at a level close
        self.close_settle = close_settle                   # settle window (s) used at a level close
        self.prominence = prominence                       # peak_db − floor, dB
        self.rejected = rejected                           # rumble too short for its loudness — noise

def gate_windows(
    env_db: np.ndarray, attacks: list[int], gate_db: float | None, floors: list[float],
    hop_s: float, release_s: float, arm_drop: float, settle_s: float, settle_min_s: float,
    level_slope: float, cap_s: float
) -> list[Window]:
    n = len(env_db)
    cap = round(cap_s / hop_s)
    release = max(1, round(release_s / hop_s))
    peak_win = max(1, round(_PEAK_WIN_S / hop_s))

    windows: list[Window] = []
    i = 0
    while i < len(attacks):
        a = attacks[i]
        floor_ref = gate_db if gate_db is not None else floors[i]
        peak_db = float(np.max(env_db[a:min(n, a + peak_win)]))
        span = max(1e-6, peak_db - floor_ref)

        below = 0
        drop_start = min(n - 1, a + cap)
        armed: int | None = None
        end, closed_by = min(n - 1, a + cap), 'cap'
        close_slope = close_settle = None
        for f in range(a, min(n, a + cap)):
            if gate_db is not None:
                if env_db[f] < gate_db:
                    if below == 0:
                        drop_start = f
                    below += 1
                    if below >= release:
                        end, closed_by = drop_start, 'gate'
                        break
                else:
                    below = 0
            if armed is None and (peak_db - env_db[f]) >= arm_drop * span:
                armed = f
            if armed is not None:
                level_frac = min(1.0, max(0.0, (env_db[f] - floor_ref) / span))
                settle = settle_min_s + level_frac * (settle_s-settle_min_s)
                s = slope_at(env_db, f, max(2, round(settle / hop_s)), hop_s)
                if -level_slope < s < level_slope:         # a plateau — not still falling, not rising
                    end, closed_by = f, 'level'
                    close_slope, close_settle = s, settle
                    break

        prominence = peak_db - floor_ref
        windows.append(
            Window(a, end, closed_by, armed, peak_db, close_slope, close_settle, prominence)
        )
        i += 1
        while i < len(attacks) and attacks[i] <= end:
            i += 1
    return windows

def auto_reject_threshold(ratios: list[float], gap_factor: float = 2.0) -> float:
    vals = np.array(sorted(r for r in ratios if r > 0 and np.isfinite(r)))
    if len(vals) < 4:
        return 0.0
    logs = np.log(vals)
    gaps = np.diff(logs)
    half = max(1, len(gaps) // 2)
    j = int(np.argmax(gaps[:half]))                        # widest gap in the lower half
    if gaps[j] < gap_factor * float(np.median(gaps)):
        return 0.0
    return float(np.exp(0.5 * (logs[j] + logs[j + 1])))    # valley midpoint

def _load_cpp_windows(session, media_id: int, version: str | None) -> list[tuple[float, float]]:
    q = (
        select(Exemplar, DetectorRun).join(DetectorRun,
        DetectorRun.id == Exemplar.detector_run_id).where(Exemplar.source_media_id == media_id
                                                         ).order_by(Exemplar.start_s)
    )
    if version is not None:
        q = q.where(DetectorRun.detector_version == version)
    rows = list(session.execute(q).all())
    if not rows:
        return []
    if version is None:
        latest = max(run.id for _, run in rows)
        rows = [(ex, run) for ex, run in rows if run.id == latest]
    return [(ex.start_s, ex.end_s) for ex, _ in rows]

def main() -> None:
    ap = argparse.ArgumentParser(
        description='Sandbox for the thunder rumble-end chain (C++ ThunderDetector is production)'
    )
    ap.add_argument('freesound_id', type=str, help='freesound external_id / wav stem')
    ap.add_argument('--rise', type=float, default=12.0, help='dB rise over floor for a clap attack')
    ap.add_argument('--attack', type=float, default=0.05, help='seconds above the level to open')
    ap.add_argument(
        '--release',
        type=float,
        default=3.0,
        help='seconds below the gate to close (DetectorConfig.release_s)'
    )
    ap.add_argument(
        '--arm-drop',
        type=float,
        default=0.9,
        help='fraction of peak->floor span that arms the release listener (DetectorConfig.arm_drop)'
    )
    ap.add_argument(
        '--settle',
        type=float,
        default=2.0,
        help='longest settle (s) — slope window at peak level',
    )
    ap.add_argument(
        '--settle-min',
        type=float,
        default=0.5,
        help='shortest settle (s) — slope window near the floor',
    )
    ap.add_argument(
        '--level-slope',
        type=float,
        default=1.0,
        help='dB/s — plateau when |forward slope| < this',
    )
    ap.add_argument(
        '--min-rumble-per-db',
        type=_ratio_or_auto,
        default='auto',
        help="reject when rumble(s)/prominence(dB) is below this; 'auto' uses valley, 0 disables",
    )
    ap.add_argument(
        '--gate-mode',
        choices=('floor-median', 'under-median-mean'),
        default='under-median-mean',
        help='gate level (C++ uses quiet-half-mean ≈ under-median-mean)',
    )
    ap.add_argument(
        '--min-prominence',
        type=float,
        default=0.0,
        help='drop attacks below this fraction of recording dynamic range; 0 keeps all',
    )
    ap.add_argument('--lookback', type=float, default=1.0, help='pre-attack floor window, seconds')
    ap.add_argument('--refractory', type=float, default=2.0, help='min gap between raw attacks, s')
    ap.add_argument(
        '--cpp',
        action='store_true',
        help='overlay C++ exemplar windows from the DB (latest run for this media)',
    )
    ap.add_argument(
        '--version',
        default=None,
        help='with --cpp: only this detector_version (default: latest run)',
    )
    ap.add_argument('--fft', type=int, default=2048, help='spectrogram FFT size')
    ap.add_argument('--hop', type=int, default=512, help='spectrogram hop size')
    ap.add_argument('--fmax', type=float, default=8000.0, help='max frequency shown, Hz')
    ap.add_argument('--save', type=Path, default=None, help='save PNG instead of showing')
    args = ap.parse_args()

    with SessionLocal() as session:
        media = session.scalar(
            select(Media).where(
            Media.origin == MediaOrigin.freesound,
            Media.external_id == str(args.freesound_id),
            )
        )
        if media is None:
            sys.exit(f'no freesound media with external_id={args.freesound_id}')
        audio = media.audio
        if audio is None:
            sys.exit(f'freesound {args.freesound_id} has no audio blob')
        marks = list(media.marks or [])
        x = decode_media_bytes(audio, media.sample_rate)
        sr = media.sample_rate
        cpp_windows = (_load_cpp_windows(session, media.id, args.version) if args.cpp else [])

    if args.cpp and not cpp_windows:
        print(
            f'note: --cpp set but no exemplars for freesound {args.freesound_id} — '
            f'run `sferic detect thunder --curated` first',
            file=sys.stderr,
        )

    env_hop = round(sr * _ENV_HOP_S)
    env = envelope_db(x, env_hop, round(_ENV_SMOOTH_S / _ENV_HOP_S))
    t_env = np.arange(len(env)) * env_hop / sr

    def secs(frame: int) -> float:
        return frame * env_hop / sr

    attacks, floor = detect_attacks(
        env, _ENV_HOP_S, args.rise, args.lookback, args.attack, args.refractory
    )
    floors = pre_attack_floors(env, attacks, _ENV_HOP_S, args.lookback, guard_s=0.1)
    n_raw = len(attacks)
    attacks, floors = prominence_gate(env, attacks, floors, _ENV_HOP_S, args.min_prominence)
    gate_db = compute_gate(env, floors, len(attacks), args.gate_mode)
    windows = gate_windows(
        env,
        attacks,
        gate_db,
        floors,
        _ENV_HOP_S,
        args.release,
        args.arm_drop,
        args.settle,
        args.settle_min,
        args.level_slope,
        _RUMBLE_MAX_S,
    )

    ratios = [(secs(w.end) - secs(w.onset)) / w.prominence if w.prominence > 0 else float('inf')
        for w in windows]
    reject_thr = (
        auto_reject_threshold(ratios) if args.min_rumble_per_db == 'auto' else
        args.min_rumble_per_db
    )
    for w, r in zip(windows, ratios):
        w.rejected = r < reject_thr
    kept = [w for w in windows if not w.rejected]

    gated = f' ({n_raw}->{len(attacks)} after prominence gate)' if args.min_prominence > 0 else ''
    print(
        f'freesound {args.freesound_id}: {len(attacks)} attacks{gated} -> {len(kept)} kept '
        f'({len(windows) - len(kept)} rejected) windows (expected {len(marks)})' +
        (f', cpp {len(cpp_windows)}' if args.cpp else '')
    )
    print(
        f'gate = {"disabled (single clap)" if gate_db is None else f"{gate_db:.1f} dB"} '
        f'[{args.gate_mode}, {len(floors)} floors]'
    )
    print(
        f'reject ratio < {reject_thr:.3f} '
        f'[{"auto valley" if args.min_rumble_per_db == "auto" else "manual"}]'
    )
    for w in windows:
        rumble = secs(w.end) - secs(w.onset)
        ratio = rumble / w.prominence if w.prominence > 0 else float('inf')
        arm = '' if w.armed is None else f'  armed {secs(w.armed):.1f}s'
        lvl = (
            '' if w.close_slope is None else
            f'  slope {w.close_slope:+.2f} dB/s @ settle {w.close_settle:.1f}s'
        )
        tag = ' REJECT' if w.rejected else ''
        print(
            f'  {secs(w.onset):6.1f}s -> {secs(w.end):6.1f}s  ({rumble:4.1f}s)  '
            f'peak {w.peak_db:5.1f}dB  prom {w.prominence:4.1f}dB  rumble/prom {ratio:4.2f}  '
            f'[{w.closed_by}]{arm}{lvl}{tag}'
        )

    S_db = to_db(stft(x, args.fft, args.hop))
    freq = np.fft.rfftfreq(args.fft, d=1.0 / sr).astype(np.float64)
    t_spec = np.arange(S_db.shape[1]) * args.hop / sr
    fmax_idx = int(np.searchsorted(freq, args.fmax))
    vmax = float(S_db[1:fmax_idx].max()) if fmax_idx > 1 else 0.0

    fig = plt.figure(figsize=(16, 11.5))
    gs = fig.add_gridspec(3, 1, height_ratios=[3, 2, 1.5], hspace=0.32)
    ax_spec = fig.add_subplot(gs[0])
    ax_env = fig.add_subplot(gs[1], sharex=ax_spec)
    ax_table = fig.add_subplot(gs[2])
    ax_spec.tick_params(labelbottom=False)

    f = freq[1:]
    mask = f <= args.fmax
    im = ax_spec.pcolormesh(
        t_spec,
        f[mask],
        S_db[1:][mask],
        cmap=_CMAP,
        vmin=vmax - 80.0,
        vmax=vmax,
        shading='auto',
        rasterized=True,
    )
    ax_spec.set_yscale('log')
    ax_spec.set_ylim(_F_MIN, args.fmax)
    ax_spec.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f'{int(v):,}'))
    ax_spec.set_ylabel('Frequency (Hz)')
    ax_spec.axhline(_THUNDER_CEIL_HZ, color='#8ab4f8', linewidth=0.8, linestyle=':', alpha=0.7)

    for ax in (ax_spec, ax_env):
        for w in windows:
            if w.rejected:
                ax.axvspan(secs(w.onset), secs(w.end), color=_C_REJECT, alpha=0.06)
                ax.axvline(secs(w.onset), color=_C_REJECT, linewidth=1.0, linestyle=':')
                ax.axvline(secs(w.end), color=_C_REJECT, linewidth=1.0, linestyle=':')
            else:
                colour = _END_COLORS[w.closed_by]
                ax.axvspan(secs(w.onset), secs(w.end), color=colour, alpha=0.10)
                ax.axvline(secs(w.onset), color=_C_ONSET, linewidth=1.1)
                ax.axvline(secs(w.end), color=colour, linewidth=1.1, linestyle='--')
        for start_s, end_s in cpp_windows:
            ax.axvline(start_s, color=_C_CPP, linewidth=1.0, linestyle='-.')
            ax.axvline(end_s, color=_C_CPP, linewidth=0.8, linestyle=':', alpha=0.7)
        for m in marks:
            ax.axvline(m, color=_C_EXPECTED, linewidth=1.1, linestyle='--')

    for i, w in enumerate(kept):
        ax_spec.text(
            secs(w.onset),
            0.98,
            _label(i),
            transform=ax_spec.get_xaxis_transform(),
            fontsize=8,
            fontweight='bold',
            color=_END_COLORS[w.closed_by],
            va='top',
            ha='left',
            clip_on=True,
        )

    ax_env.plot(t_env, env, color='#c8ccd0', linewidth=0.7, label='RMS (dB)')
    ax_env.plot(t_env, floor, color='#6b7280', linewidth=0.7, linestyle=':', label='trailing floor')
    if gate_db is not None:
        ax_env.axhline(gate_db, color=_C_GATE, linewidth=1.2, label=f'gate {gate_db:.1f} dB')
    ax_env.scatter(
        [secs(a) for a in attacks],
        floors,
        s=18,
        color=_C_GATE,
        zorder=5,
        label='pre-attack floor',
    )
    armed_frames = [armed for w in windows if (armed := w.armed) is not None]
    if armed_frames:
        ax_env.scatter(
            [secs(f) for f in armed_frames],
            [env[f] for f in armed_frames],
            s=28,
            marker='x',
            color=_END_COLORS['level'],
            zorder=6,
            label=f'listener armed ({int(args.arm_drop * 100)}% drop)',
        )
    ax_env.set_ylabel('RMS (dB)')
    ax_env.set_xlabel('Time (s)')
    ax_env.grid(True, alpha=0.2, linewidth=0.5)
    ax_env.legend(loc='upper right', fontsize=7, ncol=2)

    legend_handles = [
        Line2D([], [], color=_C_EXPECTED, linestyle='--', label=f'expected ({len(marks)})'),
        Line2D([], [], color=_C_ONSET, label='python attack'),
        Line2D([], [], color=_END_COLORS['gate'], linestyle='--', label='gate close'),
        Line2D([], [], color=_END_COLORS['level'], linestyle='--', label='level close'),
        Line2D([], [], color=_C_REJECT, linestyle=':', label='rejected (noise)'),
    ]
    if args.cpp:
        legend_handles.append(
            Line2D(
            [],
            [],
            color=_C_CPP,
            linestyle='-.',
            label=f'cpp windows ({len(cpp_windows)})',
            )
        )
    ax_spec.legend(handles=legend_handles, loc='upper right', fontsize=8)

    ax_table.axis('off')
    ax_table.text(
        0.0,
        0.98,
        'python strikes  ·  id · start (m:ss) · length · peak · '
        'prominence · rumble/prom · close',
        transform=ax_table.transAxes,
        fontsize=9,
        fontweight='bold',
        va='top',
        ha='left',
    )
    header = f'{"":<3}{"start":>6}{"len":>8}{"peak":>9}{"prom":>8}{"r/p":>7}  close'
    rows = []
    for i, w in enumerate(kept):
        start = secs(w.onset)
        length = secs(w.end) - start
        mmss = f'{int(start // 60)}:{int(start % 60):02d}'
        ratio = length / w.prominence if w.prominence > 0 else float('inf')
        rows.append(
            f'{_label(i):<3}{mmss:>6}{length:7.1f}s{w.peak_db:8.1f}dB'
            f'{w.prominence:6.1f}dB{ratio:7.2f}  {w.closed_by}'
        )
    ncols = 1 if len(rows) <= 16 else (2 if len(rows) <= 32 else 3)
    per = -(-len(rows) // ncols) if rows else 0
    for c in range(ncols):
        chunk = rows[c * per:(c+1) * per]
        if chunk:
            ax_table.text(
                0.01 + c * (0.99/ncols),
                0.78,
                header + '\n' + '\n'.join(chunk),
                transform=ax_table.transAxes,
                family='monospace',
                fontsize=8,
                va='top',
                ha='left',
            )

    cpp_bit = f', cpp {len(cpp_windows)}' if args.cpp else ''
    fig.suptitle(
        f'freesound {args.freesound_id} — {len(kept)} python kept vs {len(marks)} expected '
        f'({len(attacks)} attacks, {len(windows) - len(kept)} rejected{cpp_bit})',
        fontsize=12,
        fontweight='bold',
        y=0.995,
    )
    fig.colorbar(
        im, ax=[ax_spec, ax_env, ax_table], label='Magnitude (dB)', pad=0.01, fraction=0.015
    )

    show_or_save(fig, args.save)

if __name__ == '__main__':
    main()
