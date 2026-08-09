from __future__ import annotations

import json
import subprocess
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from yt_dlp import YoutubeDL

from ingest.audioset import Segment
from ingest.config import LIBRARY_CODEC, LIBRARY_SR

# info_dict keys that are bulky and worthless after the fact
# dropped before the row is stored as ytvideo.raw_info.
_RAW_INFO_DROP = frozenset({
    "formats", "thumbnails", "automatic_captions", "subtitles", "heatmap", "requested_formats"
})

@dataclass(frozen=True, slots=True)
class FlacInfo:
    sample_rate: int
    channels: int

def _section_ranges(start_s: float, end_s: float):
    """yt-dlp `download_ranges` callback for one [start, end] window in seconds."""

    def _ranges(_info: object, _ydl: object) -> Iterator[dict[str, float]]:
        yield {"start_time": start_s, "end_time": end_s}

    return _ranges

def _opts(seg: Segment, cache_dir: Path) -> dict[str, Any]:
    return {
        "format": "bestaudio/best",
        "paths": {
            "home": str(cache_dir)
        },
        "outtmpl": f"%(id)s_{int(seg.start_s)}-{int(seg.end_s)}.%(ext)s",
        "noplaylist": True,
        "download_ranges": _section_ranges(seg.start_s, seg.end_s),
        "force_keyframes_at_cuts": True,
        "postprocessors": [{
            "key": "FFmpegExtractAudio",
            "preferredcodec": LIBRARY_CODEC
        }],
        "postprocessor_args": {
            "ffmpeg": ["-ar", str(LIBRARY_SR)]
        },
        "retries": 10,
        "ratelimit": 500_000,
        "sleep_interval": 1,
        "max_sleep_interval": 5,
        "quiet": True,
        "no_warnings": True,
    }


def probe(path: Path) -> FlacInfo:
    """Read the real rate and channel count off the produced file.

    Rejects anything that did not land at the library sample rate.
    """

    out = subprocess.run([
        "ffprobe",
        "-v",
        "error",
        "-select_streams",
        "a:0",
        "-show_entries",
        "stream=sample_rate,channels",
        "-of",
        "json",
        str(path),
    ],
        capture_output=True,
        text=True,
        check=True)
    stream = json.loads(out.stdout)["streams"][0]
    fmt = FlacInfo(sample_rate=int(stream["sample_rate"]), channels=int(stream["channels"]))
    if fmt.sample_rate != LIBRARY_SR:
        raise RuntimeError(
            f"{path}: expected {LIBRARY_SR} Hz, got {fmt.sample_rate}"
        )
    return fmt


def strip_raw_info(info: dict[str, Any]) -> dict[str, Any]:
    return {k: v for k, v in info.items() if k not in _RAW_INFO_DROP}


def download(seg: Segment, cache_dir: Path) -> tuple[Path, dict[str, Any]]:
    """Fetch one segment. Returns the cached audio path and the info_dict."""

    params: Any = _opts(seg, cache_dir)
    with YoutubeDL(params) as ydl:
        info: Any = ydl.extract_info(seg.url, download=True)

    if not isinstance(info, dict):
        raise RuntimeError(f"yt-dlp returned no info for {seg.ytid}")

    downloads = info.get("requested_downloads")
    if not isinstance(downloads, list) or not downloads:
        raise RuntimeError(f"yt-dlp produced no downloads for {seg.ytid}")

    first = downloads[0]
    filepath = first.get("filepath") if isinstance(first, dict) else None
    if not isinstance(filepath, str):
        raise RuntimeError(f"yt-dlp download missing filepath for {seg.ytid}")

    result: dict[str, Any] = dict(info)
    return Path(filepath), result
