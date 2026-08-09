"""
Ingest into `media`, choosing a corpus with --source:

  audioset  → reference corpus (weak-labelled YouTube segments; needs --split)
  freesound → curated corpus (hand-picked full recordings on disk)

python -m ingest --source audioset  --split eval --domain thunder
python -m ingest --source audioset  --split eval --domain thunder --retry-failed
python -m ingest --source freesound --domain thunder

AudioSet downloads each matching segment to the cache, writes it into `media` at
the library codec/rate, then deletes the cache file. Freesound transcodes each
local recording to the same format and writes it as a `curated` row. A per-item
failure is recorded in `ingest_log` and the batch continues — every other failure
stops the run. `--retry-failed` (audioset only) narrows to segments that previously
logged an error and still have no `media` row.
"""

from __future__ import annotations

import argparse
from enum import StrEnum
from itertools import islice
from pathlib import Path

from db.models import MediaOrigin
from db.session import SessionLocal
from ingest import config, freesound, ytdl
from ingest.audioset import Segment, Split, read_segments
from ingest.freesound import CuratedFile
from ingest.labels import Domain, load_vocabulary, target_mids
from ingest.metadata import YTFields
from ingest.writer import (curated_domain_label, failed_segment_keys, log_curated_failure,
                           log_failure, sync_vocabulary, write, write_curated)

class Source(StrEnum):
    audioset = "audioset"
    freesound = "freesound"

def ingest_one(seg: Segment, cache_dir: Path, label_ids: dict[str, int]) -> None:
    path, info = ytdl.download(seg, cache_dir)
    fmt = ytdl.probe(path)
    audio = path.read_bytes()
    meta = YTFields.of(info, seg, ytdl.strip_raw_info(info))

    with SessionLocal() as session, session.begin():
        media_id = write(session, seg, audio, fmt, meta, label_ids)
    path.unlink()
    print(
        f"[ok]   {seg.ytid} {seg.start_s:.0f}-{seg.end_s:.0f} → media {media_id} "
        f"({len(audio)} bytes, {fmt.channels} ch)"
    )

def run(split: Split, domain: Domain, limit: int | None, retry_failed: bool) -> None:
    audioset_dir = config.audioset_dir()
    cache_dir = config.cache_dir()
    cache_dir.mkdir(parents=True, exist_ok=True)

    with SessionLocal() as session, session.begin():
        label_ids = sync_vocabulary(session, load_vocabulary(audioset_dir))
    print(f"[vocab] {len(label_ids)} labels")

    segments = read_segments(split.csv_path(audioset_dir), target_mids(domain))

    if retry_failed:
        with SessionLocal() as session:
            pending = failed_segment_keys(session, MediaOrigin.youtube)
        print(f"[retry] {len(pending)} previously-failed segments pending")
        segments = (s for s in segments if (s.ytid, s.start_s, s.end_s) in pending)

    failures = 0

    for seg in islice(segments, limit):
        try:
            ingest_one(seg, cache_dir, label_ids)
        except Exception as e:
            failures += 1
            print(f"[fail] {seg.ytid} {seg.start_s:.0f}-{seg.end_s:.0f}: {type(e).__name__}: {e}")
            with SessionLocal() as session, session.begin():
                log_failure(session, seg, e)

    print(f"[done] {split} / {domain} — {failures} failed")

def ingest_curated_one(cf: CuratedFile, cache_dir: Path, label_id: int) -> None:
    path, fmt = freesound.transcode(cf.path, cache_dir)
    audio = path.read_bytes()

    with SessionLocal() as session, session.begin():
        media_id = write_curated(session, cf, audio, fmt, label_id)
    path.unlink()
    print(
        f"[ok]   freesound {cf.freesound_id} → media {media_id} "
        f"({len(audio)} bytes, {fmt.channels} ch @ {fmt.sample_rate} Hz)"
    )

def run_freesound(domain: Domain, limit: int | None) -> None:
    freesound_dir = config.freesound_dir()
    cache_dir = config.cache_dir()
    cache_dir.mkdir(parents=True, exist_ok=True)

    with SessionLocal() as session, session.begin():
        label_id = curated_domain_label(session, domain)
    print(f"[label] curated:{domain} → id {label_id}")

    failures = 0
    for cf in islice(freesound.scan(freesound_dir, domain), limit):
        try:
            ingest_curated_one(cf, cache_dir, label_id)
        except Exception as e:
            failures += 1
            print(f"[fail] freesound {cf.freesound_id}: {type(e).__name__}: {e}")
            with SessionLocal() as session, session.begin():
                log_curated_failure(session, cf, e)

    print(f"[done] freesound / {domain} — {failures} failed")

def main() -> None:
    p = argparse.ArgumentParser(description="ingest reference (audioset) / curated (freesound) → Postgres")
    p.add_argument("--source", type=Source, choices=list(Source), default=Source.audioset,
                   help="audioset → reference corpus (needs --split); freesound → curated corpus")
    p.add_argument("--split", type=Split, choices=list(Split), default=Split.eval)
    p.add_argument("--domain", type=Domain, choices=list(Domain), default=Domain.thunder)
    p.add_argument("--limit", type=int, help="stop after N items")
    p.add_argument(
        "--retry-failed",
        action="store_true",
        help="audioset only — retry segments with a logged failure and no media row"
    )
    args = p.parse_args()
    if args.source is Source.freesound:
        run_freesound(args.domain, args.limit)
    else:
        run(args.split, args.domain, args.limit, args.retry_failed)

if __name__ == "__main__":
    main()
