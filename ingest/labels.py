from __future__ import annotations

import csv
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path

class Domain(StrEnum):
    thunder = "thunder"
    rain = "rain"
    wind = "wind"
    ocean = "ocean"

# Only thunder is populated
# other modules are out of scope until explicitly started.
DOMAIN_MIDS: dict[str, Domain] = {
    "/m/0jb2l": Domain.thunder,      # Thunderstorm
    "/m/0ngt1": Domain.thunder,      # Thunder
}

@dataclass(frozen=True, slots=True)
class Label:
    mid: str
    name: str
    domain: Domain | None

def load_vocabulary(audioset_dir: Path) -> dict[str, Label]:
    """mid to Label for the whole AudioSet ontology."""

    vocab: dict[str, Label] = {}
    with open(audioset_dir / "class_labels_indices.csv", newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            mid = row["mid"]
            vocab[mid] = Label(mid=mid, name=row["display_name"], domain=DOMAIN_MIDS.get(mid))
    return vocab

def target_mids(domain: Domain) -> frozenset[str]:
    return frozenset(mid for mid, d in DOMAIN_MIDS.items() if d is domain)
