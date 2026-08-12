#!/usr/bin/env python3
"""Overlay media.marks (ground truth) against detector exemplars from the DB.

Reads marks + audio from media, detections from exemplar, centroid tracks from
exemplar_feature.

Populate detections:
    sferic detect thunder --curated --force

By default the latest detector_run that produced exemplars for this media is
shown

    python3 tools/detect_plot.py <id>
    python3 tools/detect_plot.py <id> --version <v> --save path/file.png
    python3 tools/detect_plot.py <id> --all-runs
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

from spectral_common import decode_media_bytes, rms_curve, show_or_save, stft, to_db # noqa: E402
from db.session import SessionLocal                                                  # noqa: E402
from db.models import DetectorRun, Exemplar, ExemplarFeature, Media, MediaOrigin     # noqa: E402

_THUNDER_CEIL_HZ = 230.0               # thunder_constants.h FUNDAMENTAL_CEIL_HZ
_CMAP = 'magma'
_F_MIN = 20.0

# mark / verdict colours
_C_EXPECTED = '#4ad66d'                # green — hand-annotated ground truth
_C_PENDING = '#e5484d'                 # red — unreviewed
_C_APPROVED = '#3b82f6'                # blue — human approved
_C_REJECTED = '#6b7280'                # grey — human rejected

def _verdict_color(ex: Exemplar) -> str:
    if ex.approved is True:
        return _C_APPROVED
    if ex.approved is False:
        return _C_REJECTED
    return _C_PENDING

def _verdict_label(ex: Exemplar) -> str:
    if ex.approved is True:
        return 'approved'
    if ex.approved is False:
        reason = ex.reject_reason or 'reject'
        return f'reject:{reason}'
    return 'pending'

def _load(session, freesound_id: str, version: str | None, all_runs: bool):
    media = session.scalar(
        select(Media).where(
        Media.origin == MediaOrigin.freesound,
        Media.external_id == freesound_id,
        )
    )
    if media is None:
        sys.exit(f'no freesound media with external_id={freesound_id}')

    q = (
        select(Exemplar, DetectorRun).join(DetectorRun,
        DetectorRun.id == Exemplar.detector_run_id).where(Exemplar.source_media_id == media.id
                                                         ).order_by(Exemplar.start_s)
    )
    if version is not None:
        q = q.where(DetectorRun.detector_version == version)

    rows = list(session.execute(q).all())
    if not rows and not all_runs and version is None:
        # no exemplars at all
        return media, [], None

    run_label: str | None = None
    if not all_runs and version is None and rows:
        # latest run among those that actually have exemplars for this media
        latest_run_id = max(run.id for _, run in rows)
        rows = [(ex, run) for ex, run in rows if run.id == latest_run_id]
        run = rows[0][1]
        run_label = f'{run.detector_name}@{run.detector_version} (run {run.id})'
    elif version is not None and rows:
        run = rows[0][1]
        run_label = f'{run.detector_name}@{run.detector_version}'
        if all_runs:
            run_label += ' (all runs of this version)'
    elif all_runs and rows:
        versions = sorted({run.detector_version for _, run in rows})
        run_label = f'all runs ({", ".join(versions)})'

    exemplars = [ex for ex, _ in rows]
    tracks = []
    for ex in exemplars:
        feat = list(
            session.scalars(
            select(ExemplarFeature).where(ExemplarFeature.exemplar_id == ex.id
                                         ).order_by(ExemplarFeature.frame_index)
            )
        )
        tracks.append((
            np.array([r.t_s for r in feat], dtype=np.float64),
            np.array([r.centroid_hz for r in feat], dtype=np.float64),
        ))
    return media, list(zip(exemplars, tracks)), run_label

def main() -> None:
    ap = argparse.ArgumentParser(description='Expected marks vs detector exemplars.')
    ap.add_argument('freesound_id', type=str, help='freesound external_id / wav stem')
    ap.add_argument('--version', default=None, help='only this detector_version (e.g. v11)')
    ap.add_argument('--all-runs',action='store_true',help='show every exemplar for this media (default: latest run only)')
    ap.add_argument('--fft', type=int, default=2048, help='STFT FFT size')
    ap.add_argument('--hop', type=int, default=512, help='STFT hop size')
    ap.add_argument('--fmax', type=float, default=8000.0, help='max frequency shown, Hz')
    ap.add_argument('--save', type=Path, default=None, help='save PNG instead of showing')
    args = ap.parse_args()

    with SessionLocal() as session:
        media, pairs, run_label = _load(session, args.freesound_id, args.version, args.all_runs)
        audio = media.audio
        if audio is None:
            sys.exit(f'freesound {args.freesound_id} has no audio blob')
        marks = list(media.marks or [])
        x = decode_media_bytes(audio, media.sample_rate)
        sr = media.sample_rate
    if not marks:
        print(
            f'note: no marks on freesound {args.freesound_id} — '
            f'ingest with `python -m ingest.marks {args.freesound_id} <onsets...>`',
            file=sys.stderr,
        )
    if not pairs:
        print(
            f'note: no exemplars for freesound {args.freesound_id} — '
            f'run `sferic detect thunder --curated` first',
            file=sys.stderr,
        )
    n_pending = sum(1 for ex, _ in pairs if ex.approved is None)
    n_approved = sum(1 for ex, _ in pairs if ex.approved is True)
    n_rejected = sum(1 for ex, _ in pairs if ex.approved is False)

    S_db = to_db(stft(x, args.fft, args.hop))
    freq = np.fft.rfftfreq(args.fft, d=1.0 / sr).astype(np.float64)
    t_spec = np.arange(S_db.shape[1]) * args.hop / sr
    rms = rms_curve(x, args.hop)
    t_rms = np.arange(len(rms)) * args.hop / sr

    fmax_idx = int(np.searchsorted(freq, args.fmax))
    vmax = float(S_db[1:fmax_idx].max()) if fmax_idx > 1 else 0.0
    vmin = vmax - 80.0

    fig, (ax_spec, ax_rms) = plt.subplots(
        2,
        1,
        figsize=(16, 8),
        sharex=True,
        gridspec_kw={
        'height_ratios': [3, 1],
        'hspace': 0.08
        },
    )

    f = freq[1:]
    mask = f <= args.fmax
    im = ax_spec.pcolormesh(
        t_spec,
        f[mask],
        S_db[1:][mask],
        cmap=_CMAP,
        vmin=vmin,
        vmax=vmax,
        shading='auto',
        rasterized=True,
    )
    ax_spec.set_yscale('log')
    ax_spec.set_ylim(_F_MIN, args.fmax)
    ax_spec.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f'{int(v):,}'))
    ax_spec.set_ylabel('Frequency (Hz)')
    ax_spec.axhline(_THUNDER_CEIL_HZ, color='#8ab4f8', linewidth=0.8, linestyle=':', alpha=0.7)

    for ex, (t_c, c_hz) in pairs:
        colour = _verdict_color(ex)
        ax_spec.axvspan(ex.start_s, ex.end_s, color=colour, alpha=0.08)
        ax_spec.axvline(ex.start_s, color=colour, linewidth=1.1)
        conf = float('nan') if ex.confidence is None else ex.confidence
        label = f' {conf:.2f}'
        if ex.approved is not None:
            label += f' {_verdict_label(ex)}'
        ax_spec.text(
            ex.start_s,
            args.fmax,
            label,
            color=colour,
            fontsize=7,
            va='top',
            ha='left',
        )
        if len(t_c):
            ax_spec.plot(t_c, c_hz, color='white', linewidth=0.7, alpha=0.6)

    for m in marks:
        ax_spec.axvline(m, color=_C_EXPECTED, linewidth=1.1, linestyle='--')

    ax_rms.plot(t_rms, 20.0 * np.log10(rms + 1e-9), linewidth=0.8, color='#4a9eda')
    ax_rms.set_ylim(-90, 3)
    ax_rms.set_ylabel('RMS (dB)', fontsize=8)
    ax_rms.set_xlabel('Time (s)')
    ax_rms.grid(True, alpha=0.25, linewidth=0.5)
    for m in marks:
        ax_rms.axvline(m, color=_C_EXPECTED, linewidth=1.0, linestyle='--')
    for ex, _ in pairs:
        ax_rms.axvline(ex.start_s, color=_verdict_color(ex), linewidth=1.0)

    handles = [
        Line2D(
        [],
        [],
        color=_C_EXPECTED,
        linestyle='--',
        label=f'expected ({len(marks)})',
        ),
        Line2D([], [], color=_C_PENDING, label=f'pending ({n_pending})'),
        Line2D([], [], color=_C_APPROVED, label=f'approved ({n_approved})'),
        Line2D([], [], color=_C_REJECTED, label=f'rejected ({n_rejected})'),
    ]
    ax_spec.legend(handles=handles, loc='upper right', fontsize=8)

    run_bit = f' — {run_label}' if run_label else ''
    fig.suptitle(
        f'freesound {args.freesound_id} — expected {len(marks)}, '
        f'detected {len(pairs)}{run_bit}',
        fontsize=12,
        fontweight='bold',
        y=0.98,
    )
    fig.colorbar(im, ax=[ax_spec, ax_rms], label='Magnitude (dB)', pad=0.01, fraction=0.015)
    show_or_save(fig, args.save)

if __name__ == '__main__':
    main()
