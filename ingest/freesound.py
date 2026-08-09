from __future__ import annotations

import subprocess
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

from ingest.config import LIBRARY_CODEC, LIBRARY_SR
from ingest.labels import Domain
from ingest.ytdl import FlacInfo, probe

_AUDIO_EXTS = frozenset({".wav", ".flac", ".mp3", ".m4a", ".aif", ".aiff", ".ogg"})

@dataclass(frozen=True, slots=True)
class CuratedFile:
    path: Path
    freesound_id: str
    username: str | None
    title: str | None

    @property
    def webpage_url(self) -> str | None:
        return (f"https://freesound.org/s/{self.freesound_id}/"
                if self.freesound_id.isdigit() else None)

    @classmethod
    def of(cls, path: Path) -> "CuratedFile":
        parts = path.stem.split("__", 2)
        if len(parts) == 3:
            return cls(path=path, freesound_id=parts[0], username=parts[1], title=parts[2])
        return cls(path=path, freesound_id=path.stem, username=None, title=None)

def scan(freesound_dir: Path, domain: Domain) -> Iterator[CuratedFile]:
    for path in sorted((freesound_dir / domain.value).iterdir()):
        if path.suffix.lower() in _AUDIO_EXTS:
            yield CuratedFile.of(path)

def transcode(src: Path, cache_dir: Path) -> tuple[Path, FlacInfo]:
    """Re-encode at the library rate/codec, channels preserved (curated stereo
    carries the spatial profile). Returns the cached path and its probed format."""

    out = cache_dir / f"{src.stem}.{LIBRARY_CODEC}"
    subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
         "-i", str(src), "-ar", str(LIBRARY_SR), "-c:a", LIBRARY_CODEC, str(out)],
        check=True,
    )
    return out, probe(out)
