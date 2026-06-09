#!/usr/bin/env python3
"""Render the debug planes written by AnalysisProcessor::locate_edits.

Reads the binary dump (set LocateConfig.debug_dump_path), plots aligned source,
processed, and the difference with the located mask outlined in red — mirroring
a spectral-editor view so the mask can be eyeballed against the actual edit.
"""
import sys
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

def load(path):
    with open(path, "rb") as f:
        nf, nb = np.fromfile(f, dtype=np.int64, count=2)
        hop_s, freq_spacing = np.fromfile(f, dtype=np.float64, count=2)
        n = int(nf) * int(nb)
        sdb = np.fromfile(f, dtype=np.float64, count=n).reshape(nf, nb)
        pdb = np.fromfile(f, dtype=np.float64, count=n).reshape(nf, nb)
        mask = np.fromfile(f, dtype=np.uint8, count=n).reshape(nf, nb).astype(bool)
    return sdb, pdb, mask, float(hop_s), float(freq_spacing)

def main():
    path = sys.argv[1]
    out = sys.argv[2]
    sdb, pdb, mask, hop_s, freq_spacing = load(path)
    nf, nb = sdb.shape
    dur = nf * hop_s
    nyq_khz = nb * freq_spacing / 1000.0
    ext = [0, dur, 0, nyq_khz]
    vmax = sdb.max()
    d = sdb - pdb
    d -= np.median(d, axis=0, keepdims=True)

    fig, ax = plt.subplots(3, 1, figsize=(11, 11), sharex=True)
    ax[0].imshow(
        sdb.T, origin="lower", aspect="auto", cmap="inferno", extent=ext, vmin=vmax - 80, vmax=vmax
    )
    ax[0].set_title("source (aligned)")
    ax[1].imshow(
        pdb.T, origin="lower", aspect="auto", cmap="inferno", extent=ext, vmin=vmax - 80, vmax=vmax
    )
    ax[1].set_title("processed + located mask (red)")
    im = ax[2].imshow(
        d.T, origin="lower", aspect="auto", cmap="RdBu_r", extent=ext, vmin=-20, vmax=20
    )
    ax[2].set_title("per-bin-normalised difference  source − processed (dB)")
    for a in ax:
        a.set_ylabel("kHz")
    ax[2].set_xlabel("seconds")

    if mask.any():
        T = np.linspace(0, dur, nf)
        F = np.linspace(0, nyq_khz, nb)
        ax[1].contour(T, F, mask.T.astype(float), levels=[0.5], colors="red", linewidths=1.5)

    plt.colorbar(im, ax=ax[2], orientation="horizontal", pad=0.15, fraction=0.05)
    plt.tight_layout()
    plt.savefig(out, dpi=95)
    print(f"located {int(mask.sum())} cells -> {out}")

if __name__ == "__main__":
    main()
