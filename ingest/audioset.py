from __future__ import annotations

import csv
from collections.abc import Iterator
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path

class Split(StrEnum):
    eval = "eval"
    balanced_train = "balanced_train"
    unbalanced_train = "unbalanced_train"

    def csv_path(self, audioset_dir: Path) -> Path:
        return audioset_dir / "segments" / f"{self.value}_segments.csv"

@dataclass(frozen=True, slots=True)
class Segment:
    ytid: str
    start_s: float
    end_s: float
    mids: tuple[str, ...]

    @property
    def url(self) -> str:
        return f"https://www.youtube.com/watch?v={self.ytid}"

def read_segments(csv_path: Path, targets: frozenset[str]) -> Iterator[Segment]:
    """Yield segments carrying at least one target MID.

    Header lines start with '#'. The positive_labels field is quoted and
    comma-separated so skipinitialspace is required for the reader to see it as
    one field.
    """

    with open(csv_path, newline="", encoding="utf-8") as f:
        for row in csv.reader(f, skipinitialspace=True):
            if not row or row[0].startswith("#"):
                continue
            mids = tuple(row[3].split(","))
            if targets.isdisjoint(mids):
                continue
            yield Segment(ytid=row[0], start_s=float(row[1]), end_s=float(row[2]), mids=mids)
